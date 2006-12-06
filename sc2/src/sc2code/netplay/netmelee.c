/*
 *  Copyright 2006  Serge van den Boom <svdb@stack.nl>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "netmelee.h"
#include "libs/alarm.h"
#include "libs/callback.h"
#include "libs/log.h"
#include "libs/net.h"
#include "netplay/netinput.h"
#include "netplay/netmisc.h"
#include "netplay/netsend.h"
#include "netplay/notify.h"
#include "netplay/packetq.h"
#include "netplay/proto/npconfirm.h"
#include "netplay/proto/ready.h"
#include "netplay/proto/reset.h"

#include "build.h"
		// for StarShipPlayer()
#include "controls.h"
		// for BATTLE_INPUT_STATE
#include "init.h"
		// for NUM_PLAYERS
#include "globdata.h"
		// for GLOBAL

#include <errno.h>
#include <stdlib.h>


////////////////////////////////////////////////////////////////////////////


NetConnection *netConnections[NUM_PLAYERS];
size_t numNetConnections;

void
addNetConnection(NetConnection *conn, int playerNr) {
	netConnections[playerNr] = conn;
	numNetConnections++;
}

void
removeNetConnection(int playerNr) {
	netConnections[playerNr] = NULL;
	numNetConnections--;
}

void
closeAllConnections(void) {
	COUNT player;

	for (player = 0; player < NUM_PLAYERS; player++)
	{
		NetConnection *conn = netConnections[player];

		if (conn != NULL && NetConnection_isConnected(conn))
			closePlayerNetworkConnection(player);
	}
}

size_t
getNumNetConnections(void) {
	return numNetConnections;
}


////////////////////////////////////////////////////////////////////////////


// Check the network connections for input.
void
netInput(void) {
	uint32 timeoutMs = 0;
	NetManager_process(&timeoutMs);
			// This may cause more packets to be queued, hence the
			// flushPacketQueues().
	flushPacketQueues();

	Alarm_process();
	Callback_process();
}

void
netInputBlocking(uint32 timeoutMs) {
	uint32 nextAlarmMs;
		
	nextAlarmMs = Alarm_timeBeforeNextMs();
	if (nextAlarmMs < timeoutMs)
		timeoutMs = nextAlarmMs;

	NetManager_process(&timeoutMs);
			// This may cause more packets to be queued, hence the
			// flushPacketQueues().
	flushPacketQueues();

	Alarm_process();
	Callback_process();
}


////////////////////////////////////////////////////////////////////////////


// Send along all pending network packets.
void
flushPacketQueues(void) {
	COUNT player;

	for (player = 0; player < NUM_PLAYERS; player++)
	{
		NetConnection *conn;
		int flushStatus;

		conn = netConnections[player];
		if (conn == NULL)
			continue;

		if (!NetConnection_isConnected(conn))
			continue;

		flushStatus = flushPacketQueue(conn);
		if (flushStatus == -1 && errno != EAGAIN)
			closePlayerNetworkConnection(player);
	}
}

void
confirmConnections(void) {
	COUNT player;

	for (player = 0; player < NUM_PLAYERS; player++)
	{
		NetConnection *conn = netConnections[player];
		if (conn == NULL)
			continue;

		if (!NetConnection_isConnected(conn))
			continue;

		Netplay_confirm(conn);
	}
}

void
cancelConfirmations(void) {
	COUNT player;

	for (player = 0; player < NUM_PLAYERS; player++)
	{
		NetConnection *conn = netConnections[player];
		if (conn == NULL)
			continue;

		if (!NetConnection_isConnected(conn))
			continue;

		Netplay_cancelConfirmation(conn);
	}
}

void
connectionsLocalReady(NetConnection_ReadyCallback callback, void *arg) {
	COUNT player;

	for (player = 0; player < NUM_PLAYERS; player++)
	{
		NetConnection *conn = netConnections[player];
		if (conn == NULL)
			continue;

		if (!NetConnection_isConnected(conn))
			continue;

		Netplay_localReady(conn, callback, arg, true);
	}
}

bool
allConnected(void) {
	COUNT player;

	for (player = 0; player < NUM_PLAYERS; player++)
	{
		NetConnection *conn = netConnections[player];
		if (conn == NULL)
			continue;

		if (!NetConnection_isConnected(conn))
			return false;
	}
	return true;
}

void
sendBattleInputConnections(BATTLE_INPUT_STATE input) {
	COUNT player;

	for (player = 0; player < NUM_PLAYERS; player++)
	{
		NetConnection *conn = netConnections[player];
		if (conn == NULL)
			continue;

		if (!NetConnection_isConnected(conn))
			continue;

		Netplay_battleInput(conn, input);
	}
}

#ifdef NETPLAY_CHECKSUM
void
sendChecksumConnections(uint32 frameNr, uint32 checksum) {
	COUNT player;

	for (player = 0; player < NUM_PLAYERS; player++)
	{
		NetConnection *conn = netConnections[player];
		if (conn == NULL)
			continue;

		if (!NetConnection_isConnected(conn))
			continue;

		Netplay_sendChecksum(conn, frameNr, checksum);
	}
}
#endif  /* NETPLAY_CHECKSUM */

void
initBattleStateDataConnections(void) {
	COUNT player;

	for (player = 0; player < NUM_PLAYERS; player++)
	{
		BattleStateData *battleStateData;
		NetConnection *conn = netConnections[player];
		if (conn == NULL)
			continue;

		battleStateData =
				(BattleStateData *) NetConnection_getStateData(conn);
		battleStateData->endFrameCount = 0;
	}
}

BATTLE_INPUT_STATE
networkBattleInput(COUNT player, STARSHIPPTR StarShipPtr) {
	BattleInputBuffer *bib = getBattleInputBuffer(player);
	BATTLE_INPUT_STATE result;
	
	for (;;) {
		bool ok;

#if 0
		// This is a useful debugging trick. By enabling this #if
		// block, this side will always lag the maximum number of frames
		// behind the other side. When the remote side stops on some event
		// (a breakpoint or so), this side will stop too, waiting for input
		// in the loop below, but it won't have processed the frame that
		// triggered the event yet. If you then jump over this 'if'
		// statement here, you can walk through the decisive frames
		// manually. Works best with no input delay.
		if (bib->size <= getBattleInputDelay() + 1) {
			ok = false;
		} else
#endif
			ok = BattleInputBuffer_pop(bib, &result);
					// Get the input from the front of the
					// buffer.
		if (ok)
			break;
			
		{
			NetConnection *conn = netConnections[player];

			// First try whether there is incoming data, without blocking.
			// If there isn't any, only then give a warning, and then
			// block after all.
			netInput();
			if (!NetConnection_isConnected(conn))
			{
				// Connection aborted.
				GLOBAL(CurrentActivity) |= CHECK_ABORT;
				return (BATTLE_INPUT_STATE) 0;
			}

#if 0
			log_add(log_Warning, "NETPLAY: [%d] stalling for "
					"network input. Increase the input delay if this "
					"happens a lot.\n", player);
#endif
#define MAX_BLOCK_TIME 500
			netInputBlocking(MAX_BLOCK_TIME);
			if (!NetConnection_isConnected(conn))
			{
				// Connection aborted.
				GLOBAL(CurrentActivity) |= CHECK_ABORT;
				return (BATTLE_INPUT_STATE) 0;
			}
		}
	}

	(void) StarShipPtr;
	return result;
}

static void
deleteConnectionCallback(NetConnection *conn) {
	removeNetConnection(NetConnection_getPlayerNr(conn));
}

NetConnection *
openPlayerNetworkConnection(COUNT player, void *extra) {
	NetConnection *conn;

	assert(netConnections[player] == NULL);

	conn = NetConnection_open(player,
			&netplayOptions.peer[player], NetMelee_connectCallback,
			NetMelee_closeCallback, NetMelee_errorCallback,
			deleteConnectionCallback, extra);

	addNetConnection(conn, player);
	return conn;
}

void
shutdownPlayerNetworkConnection(COUNT player) {
	NetConnection_close(netConnections[player]);
}

void
closePlayerNetworkConnection(COUNT player) {
	assert(netConnections[player] != NULL);

	NetConnection_close(netConnections[player]);
}

// If the callback function returns 'false', the function will immediately
// return with 'false'. Otherwise it will return 'true' after calling
// the callback function for each connected player.
bool
forAllConnectedPlayers(ForAllCallback callback, void *arg) {
	COUNT player;
	
	for (player = 0; player < NUM_PLAYERS; player++)
	{
		NetConnection *conn = netConnections[player];
		if (conn == NULL)
			continue;

		if (!NetConnection_isConnected(conn))
			continue;

		if (!(*callback)(conn, arg))
			return false;
	}
	return true;
}

bool
setupInputDelay(size_t localInputDelay) {
	COUNT player;
	size_t inputDelay = localInputDelay;
	
	for (player = 0; player < NUM_PLAYERS; player++)
	{
		NetConnection *conn = netConnections[player];
		if (conn == NULL)
			continue;

		if (!NetConnection_isConnected(conn))
			continue;

		if (NetConnection_getInputDelay(conn) > inputDelay)
			inputDelay = NetConnection_getInputDelay(conn);
	}

	setBattleInputDelay(inputDelay);
	return true;
}

static bool
sendInputDelayConnection(NetConnection *conn, const size_t *delay) {
	Netplay_sendInputDelay(conn, *delay);
	return true;
}

bool
sendInputDelayConnections(size_t delay) {
	return forAllConnectedPlayers(
			(ForAllCallback) sendInputDelayConnection, &delay);
}

static bool
setStateConnection(NetConnection *conn, const NetState *state) {
	NetConnection_setState(conn, *state);
	return true;
}

bool
setStateConnections(NetState state) {
	return forAllConnectedPlayers(
			(bool(*)(NetConnection *, void *)) setStateConnection, &state);
}

static bool
sendAbortConnection(NetConnection *conn, const NetplayAbortReason *reason) {
	sendAbort(conn, *reason);
	return true;
}

bool
sendAbortConnections(NetplayAbortReason reason) {
	return forAllConnectedPlayers(
			(bool(*)(NetConnection *, void *)) sendAbortConnection, &reason);
}

static bool
resetConnection(NetConnection *conn, const NetplayResetReason *reason) {
	Netplay_localReset(conn, *reason);
	return true;
}

bool
resetConnections(NetplayResetReason reason) {
	return forAllConnectedPlayers(
			(bool(*)(NetConnection *, void *)) resetConnection, &reason);
}

/////////////////////////////////////////////////////////////////////////////

typedef struct {
	NetConnection_ReadyCallback readyCallback;
	void *readyCallbackArg;
	bool notifyRemote;
} LocalReadyConnectionArg;

static bool
localReadyConnection(NetConnection *conn, LocalReadyConnectionArg *arg) {
	Netplay_localReady(conn, arg->readyCallback, arg->readyCallbackArg,
			arg->notifyRemote);
	return true;
}

bool
localReadyConnections(NetConnection_ReadyCallback readyCallback,
		void *readyArg, bool notifyRemote) {
	LocalReadyConnectionArg arg;
	arg.readyCallback = readyCallback;
	arg.readyCallbackArg = readyArg;
	arg.notifyRemote = notifyRemote;

	return forAllConnectedPlayers(
			(bool(*)(NetConnection *, void *)) localReadyConnection, &arg);
}


/////////////////////////////////////////////////////////////////////////////

#define NETWORK_POLL_DELAY (ONE_SECOND / 24)

typedef struct NegotiateReadyState NegotiateReadyState;
struct NegotiateReadyState {
	// Common fields of INPUT_STATE_DESC, from which this structure
	// "inherits".
	BOOLEAN(*InputFunc)(PVOID pInputState);
	COUNT MenuRepeatDelay;

	NetConnection *conn;
	NetState nextState;
	bool done;
};

static BOOLEAN
negotiateReadyInputFunc(NegotiateReadyState *state) {
	netInputBlocking(NETWORK_POLL_DELAY);
	// The timing out is necessary so that immediate key presses get
	// handled while we wait. If we could do without the timeout,
	// we wouldn't even need negotiateReadyInputFunc() and the
	// DoInput() call.
	
	// No need to call flushPacketQueues(); nothing needs to be sent
	// right now.

	if (!NetConnection_isConnected(state->conn))
		return FALSE;

	return !state->done;
}

// Called when both sides are ready
static void
negotiateReadyBothReadyCallback(NetConnection *conn, void *arg) {
	NegotiateReadyState *state =(NegotiateReadyState *) arg;

	NetConnection_setState(conn, state->nextState);
			// This has to be done immediately, as more packets in the
			// receive queue may be handled by the netInput() call that
			// triggered this callback.
			// This is the reason for the nextState argument to
			// negotiateReady(); setting the state after the call to
			// negotiateReady() would be too late.
	state->done = true;
}

bool
negotiateReady(NetConnection *conn, bool notifyRemote, NetState nextState) {
	NegotiateReadyState state;
	state.InputFunc = (BOOLEAN(*)(void *)) negotiateReadyInputFunc;
	state.MenuRepeatDelay = 0;
	state.conn = conn;
	state.nextState = nextState;
	state.done = false;

	Netplay_localReady(conn, negotiateReadyBothReadyCallback,
			(void *) &state, notifyRemote);
	flushPacketQueue(conn);
	if (!state.done)
		DoInput(&state, FALSE);

	return NetConnection_isConnected(conn);
}

// Wait for all connections to get ready.
// XXX: Right now all connections are handled one by one. Handling them all
//      at once would be faster but would require more work, which is
//      not worth it as the time is minimal and this function is not
//      time critical.
bool
negotiateReadyConnections(bool notifyRemote, NetState nextState) {
	COUNT player;
	size_t numDisconnected = 0;

	for (player = 0; player < NUM_PLAYERS; player++)
	{
		NetConnection *conn = netConnections[player];
		if (conn == NULL)
			continue;

		if (!NetConnection_isConnected(conn)) {
			numDisconnected++;
			continue;
		}

		negotiateReady(conn, notifyRemote, nextState);
	}

	return numDisconnected == 0;
}

typedef struct WaitReadyState WaitReadyState;
struct WaitReadyState {
	// Common fields of INPUT_STATE_DESC, from which this structure
	// "inherits".
	BOOLEAN(*InputFunc)(PVOID pInputState);
	COUNT MenuRepeatDelay;

	NetConnection *conn;
	NetConnection_ReadyCallback readyCallback;
	void *readyCallbackArg;
	bool done;
};

static void
waitReadyCallback(NetConnection *conn, void *arg) {
	WaitReadyState *state =(WaitReadyState *) arg;
	state->done = true;

	// Call the original callback.
	state->readyCallback(conn, state->readyCallbackArg);
}

static BOOLEAN
waitReadyInputFunc(WaitReadyState *state) {
	netInputBlocking(NETWORK_POLL_DELAY);
	// The timing out is necessary so that immediate key presses get
	// handled while we wait. If we could do without the timeout,
	// we wouldn't even need negotiateReadyInputFunc() and the
	// DoInput() call.
	
	// No need to call flushPacketQueues(); nothing needs to be sent
	// right now.

	if (!NetConnection_isConnected(state->conn))
		return FALSE;

	return !state->done;
}

bool
waitReady(NetConnection *conn) {
	WaitReadyState state;
	state.InputFunc =(BOOLEAN(*)(void *)) waitReadyInputFunc;
	state.MenuRepeatDelay = 0;
	state.conn = conn;
	state.readyCallback = NetConnection_getReadyCallback(conn);
	state.readyCallbackArg = NetConnection_getReadyCallbackArg(conn);
	state.done = false;

	NetConnection_setReadyCallback(conn, waitReadyCallback, (void *) &state);

	DoInput(&state, FALSE);

	return NetConnection_isConnected(conn);
}


////////////////////////////////////////////////////////////////////////////

typedef struct WaitResetState WaitResetState;
struct WaitResetState {
	// Common fields of INPUT_STATE_DESC, from which this structure
	// "inherits".
	BOOLEAN(*InputFunc)(PVOID pInputState);
	COUNT MenuRepeatDelay;

	NetConnection *conn;
	NetState nextState;
	bool done;
};

static BOOLEAN
waitResetInputFunc(WaitResetState *state) {
	netInputBlocking(NETWORK_POLL_DELAY);
	// The timing out is necessary so that immediate key presses get
	// handled while we wait. If we could do without the timeout,
	// we wouldn't even need waitResetInputFunc() and the
	// DoInput() call.
	
	// No need to call flushPacketQueues(); nothing needs to be sent
	// right now.

	if (!NetConnection_isConnected(state->conn))
		return FALSE;

	return !state->done;
}

// Called when both sides are reset.
static void
waitResetBothResetCallback(NetConnection *conn, void *arg) {
	WaitResetState *state = (WaitResetState *) arg;

	if (state->nextState != (NetState) -1) {
		NetConnection_setState(conn, state->nextState);
		// This has to be done immediately, as more packets in the
		// receive queue may be handled by the netInput() call that
		// triggered this callback.
		// This is the reason for the nextState argument to
		// waitReset(); setting the state after the call to
		// waitReset() would be too late.
	}
	state->done = true;
}

bool
waitReset(NetConnection *conn, NetState nextState) {
	WaitResetState state;
	state.InputFunc = (BOOLEAN(*)(void *)) waitResetInputFunc;
	state.MenuRepeatDelay = 0;
	state.conn = conn;
	state.nextState = nextState;
	state.done = false;

	Netplay_setResetCallback(conn, waitResetBothResetCallback,
			(void *) &state);
	if (state.done)
		goto out;
		

	if (!Netplay_isLocalReset(conn)) {
		Netplay_localReset(conn, ResetReason_manualReset);
		flushPacketQueue(conn);
	}

	if (!state.done)
		DoInput(&state, FALSE);

out:
	return NetConnection_isConnected(conn);
}

// Wait until we have received a reset packet to all connections. If we
// ourselves have not sent a reset packet, one is sent, with reason
// 'manualReset'.
// XXX: Right now all connections are handled one by one. Handling them all
//      at once would be faster but would require more work, which is
//      not worth it as the time is minimal and this function is not
//      time critical.
// Use '(NetState) -1' for nextState to keep the current state.
bool
waitResetConnections(NetState nextState) {
	COUNT player;
	size_t numDisconnected = 0;

	for (player = 0; player < NUM_PLAYERS; player++)
	{
		NetConnection *conn = netConnections[player];
		if (conn == NULL)
			continue;

		if (!NetConnection_isConnected(conn)) {
			numDisconnected++;
			continue;
		}

		waitReset(conn, nextState);
	}

	return numDisconnected == 0;
}

////////////////////////////////////////////////////////////////////////////

