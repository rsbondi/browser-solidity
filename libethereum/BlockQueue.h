/*
	This file is part of cpp-ethereum.

	cpp-ethereum is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	cpp-ethereum is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file BlockQueue.h
 * @author Gav Wood <i@gavwood.com>
 * @date 2014
 */

#pragma once

#include <condition_variable>
#include <thread>
#include <deque>
#include <boost/thread.hpp>
#include <libdevcore/Common.h>
#include <libdevcore/Log.h>
#include <libethcore/Common.h>
#include <libdevcore/Guards.h>
#include <libethcore/Common.h>
#include <libethcore/BlockInfo.h>
#include "VerifiedBlock.h"

namespace dev
{

namespace eth
{

class BlockChain;

struct BlockQueueChannel: public LogChannel { static const char* name(); static const int verbosity = 4; };
#define cblockq dev::LogOutputStream<dev::eth::BlockQueueChannel, true>()

struct BlockQueueStatus
{
	size_t verified;
	size_t verifying;
	size_t unverified;
	size_t future;
	size_t unknown;
	size_t bad;
};

enum class QueueStatus
{
	Ready,
	Importing,
	UnknownParent,
	Bad,
	Unknown
};

/**
 * @brief A queue of blocks. Sits between network or other I/O and the BlockChain.
 * Sorts them ready for blockchain insertion (with the BlockChain::sync() method).
 * @threadsafe
 */
class BlockQueue: HasInvariants
{
public:
	BlockQueue();
	~BlockQueue();

	/// Import a block into the queue.
	ImportResult import(bytesConstRef _tx, BlockChain const& _bc, bool _isOurs = false);

	/// Notes that time has moved on and some blocks that used to be "in the future" may no be valid.
	void tick(BlockChain const& _bc);

	/// Grabs at most @a _max of the blocks that are ready, giving them in the correct order for insertion into the chain.
	/// Don't forget to call doneDrain() once you're done importing.
	void drain(std::vector<VerifiedBlock>& o_out, unsigned _max);

	/// Must be called after a drain() call. Notes that the drained blocks have been imported into the blockchain, so we can forget about them.
	/// @returns true iff there are additional blocks ready to be processed.
	bool doneDrain(h256s const& _knownBad = h256s());

	/// Notify the queue that the chain has changed and a new block has attained 'ready' status (i.e. is in the chain).
	void noteReady(h256 const& _b) { WriteGuard l(m_lock); noteReady_WITH_LOCK(_b); }

	/// Force a retry of all the blocks with unknown parents.
	void retryAllUnknown();

	/// Get information on the items queued.
	std::pair<unsigned, unsigned> items() const { ReadGuard l(m_lock); return std::make_pair(m_readySet.size(), m_unknownSet.size()); }

	/// Clear everything.
	void clear() { WriteGuard l(m_lock); DEV_INVARIANT_CHECK; Guard l2(m_verification); m_readySet.clear(); m_drainingSet.clear(); m_verified.clear(); m_unverified.clear(); m_unknownSet.clear(); m_unknown.clear(); m_future.clear(); }

	/// Return first block with an unknown parent.
	h256 firstUnknown() const { ReadGuard l(m_lock); return m_unknownSet.size() ? *m_unknownSet.begin() : h256(); }

	/// Get some infomration on the current status.
	BlockQueueStatus status() const { ReadGuard l(m_lock); Guard l2(m_verification); return BlockQueueStatus{m_verified.size(), m_verifying.size(), m_unverified.size(), m_future.size(), m_unknown.size(), m_knownBad.size()}; }

	/// Get some infomration on the given block's status regarding us.
	QueueStatus blockStatus(h256 const& _h) const;

	template <class T> Handler onReady(T const& _t) { return m_onReady.add(_t); }

	template <class T> void setOnBad(T const& _t) { m_onBad = _t; }

private:
	void noteReady_WITH_LOCK(h256 const& _b);

	bool invariants() const override;

	void verifierBody();

	mutable boost::shared_mutex m_lock;									///< General lock for the sets, m_future and m_unknown.
	h256Hash m_drainingSet;												///< All blocks being imported.
	h256Hash m_readySet;												///< All blocks ready for chain import.
	h256Hash m_unknownSet;												///< Set of all blocks whose parents are not ready/in-chain.
	std::unordered_multimap<h256, std::pair<h256, bytes>> m_unknown;	///< For blocks that have an unknown parent; we map their parent hash to the block stuff, and insert once the block appears.
	h256Hash m_knownBad;												///< Set of blocks that we know will never be valid.
	std::multimap<unsigned, std::pair<h256, bytes>> m_future;			///< Set of blocks that are not yet valid. Ordered by timestamp
	Signal m_onReady;													///< Called when a subsequent call to import blocks will return a non-empty container. Be nice and exit fast.

	mutable Mutex m_verification;										///< Mutex that allows writing to m_verified, m_verifying and m_unverified.
	std::condition_variable m_moreToVerify;								///< Signaled when m_unverified has a new entry.
	std::vector<VerifiedBlock> m_verified;				///< List of blocks, in correct order, verified and ready for chain-import.
	std::deque<VerifiedBlock> m_verifying;				///< List of blocks being verified; as long as the second component (bytes) is empty, it's not finished.
	std::deque<std::pair<h256, bytes>> m_unverified;					///< List of blocks, in correct order, ready for verification.

	std::vector<std::thread> m_verifiers;								///< Threads who only verify.
	bool m_deleting = false;											///< Exit condition for verifiers.

	std::function<void(Exception&)> m_onBad;									///< Called if we have a block that doesn't verify.
};

std::ostream& operator<<(std::ostream& _out, BlockQueueStatus const& _s);

}
}
