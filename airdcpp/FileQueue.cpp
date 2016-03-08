/*
 * Copyright (C) 2001-2016 Jacek Sieka, arnetheduck on gmail point com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "stdinc.h"

#include "FileQueue.h"
#include "SettingsManager.h"
#include "Text.h"
#include "Util.h"

#include <boost/range/algorithm/copy.hpp>

namespace dcpp {

using boost::range::for_each;
using boost::range::copy;

FileQueue::~FileQueue() { }

void FileQueue::getBloom(HashBloom& bloom) const noexcept {
	for(auto& i: tthIndex) {
		if (i.second->getBundle()) {
			bloom.add(*i.first);
		}
	}
}

pair<QueueItemPtr, bool> FileQueue::add(const string& aTarget, int64_t aSize, Flags::MaskType aFlags, QueueItemBase::Priority p, 
	const string& aTempTarget, time_t aAdded, const TTHValue& root) noexcept {

	QueueItemPtr qi = new QueueItem(aTarget, aSize, p, aFlags, aAdded, root, aTempTarget);
	auto ret = add(qi);
	return { (ret.second ? qi : ret.first->second), ret.second };
}

pair<QueueItem::StringMap::const_iterator, bool> FileQueue::add(QueueItemPtr& qi) noexcept {
	dcassert(queueSize >= 0);
	auto ret = pathQueue.emplace(const_cast<string*>(&qi->getTarget()), qi);
	if (ret.second) {
		tthIndex.emplace(const_cast<TTHValue*>(&qi->getTTH()), qi);
		if (!qi->isSet(QueueItem::FLAG_USER_LIST) && !qi->isSet(QueueItem::FLAG_CLIENT_VIEW) && !qi->isSet(QueueItem::FLAG_FINISHED)) {
			dcassert(qi->getSize() >= 0);
			queueSize += qi->getSize();
		}

		tokenQueue.emplace(qi->getToken(), qi);
	}
	return ret;
}

void FileQueue::remove(QueueItemPtr& qi) noexcept {
	//TargetMap
	auto f = pathQueue.find(const_cast<string*>(&qi->getTarget()));
	if (f != pathQueue.end()) {
		pathQueue.erase(f);
		if (!qi->isSet(QueueItem::FLAG_USER_LIST) && (!qi->isSet(QueueItem::FLAG_FINISHED) || !qi->getBundle()) && !qi->isSet(QueueItem::FLAG_CLIENT_VIEW)) {
			dcassert(qi->getSize() >= 0);
			queueSize -= qi->getSize();
		}
	}
	dcassert(queueSize >= 0);

	//TTHIndex
	auto s = tthIndex.equal_range(const_cast<TTHValue*>(&qi->getTTH()));
	dcassert(s.first != s.second);

	auto k = find(s | map_values, qi);
	if (k.base() != s.second) {
		tthIndex.erase(k.base());
	}

	// Tokens
	tokenQueue.erase(qi->getToken());
}

QueueItemPtr FileQueue::findFile(const string& target) const noexcept {
	auto i = pathQueue.find(const_cast<string*>(&target));
	return (i == pathQueue.end()) ? nullptr : i->second;
}

QueueItemPtr FileQueue::findFile(QueueToken aToken) const noexcept {
	auto i = tokenQueue.find(aToken);
	return (i == tokenQueue.end()) ? nullptr : i->second;
}

void FileQueue::findFiles(const TTHValue& tth, QueueItemList& ql_) const noexcept {
	copy(tthIndex.equal_range(const_cast<TTHValue*>(&tth)) | map_values, back_inserter(ql_));
}

void FileQueue::matchListing(const DirectoryListing& dl, QueueItem::StringItemList& ql_) const noexcept {
	matchDir(dl.getRoot(), ql_);
}

void FileQueue::matchDir(const DirectoryListing::Directory::Ptr& dir, QueueItem::StringItemList& ql) const noexcept{
	for(const auto& d: dir->directories) {
		if(!d->getAdls())
			matchDir(d, ql);
	}

	for(const auto& f: dir->files) {
		auto tp = tthIndex.equal_range(const_cast<TTHValue*>(&f->getTTH()));
		for_each(tp, [f, &ql](const pair<TTHValue*, QueueItemPtr>& tqp) {
			if (!tqp.second->isFinished() && tqp.second->getSize() == f->getSize() && find_if(ql, CompareSecond<string, QueueItemPtr>(tqp.second)) == ql.end())
				ql.emplace_back(Util::emptyString, tqp.second);
		});
	}
}

DupeType FileQueue::isFileQueued(const TTHValue& aTTH) const noexcept {
	auto qi = getQueuedFile(aTTH);
	if (qi) {
		return (qi->isFinished() ? DUPE_FINISHED_FULL : DUPE_QUEUE_FULL);
	}
	return DUPE_NONE;
}

QueueItemPtr FileQueue::getQueuedFile(const TTHValue& aTTH) const noexcept {
	auto p = tthIndex.find(const_cast<TTHValue*>(&aTTH));
	return p != tthIndex.end() ? p->second : nullptr;
}

void FileQueue::move(QueueItemPtr& qi, const string& aTarget) noexcept {
	pathQueue.erase(const_cast<string*>(&qi->getTarget()));
	qi->setTarget(aTarget);
	pathQueue.emplace(const_cast<string*>(&qi->getTarget()), qi);
}

// compare nextQueryTime, get the oldest ones
void FileQueue::findPFSSources(PFSSourceList& sl) noexcept {
	typedef multimap<time_t, pair<QueueItem::SourceConstIter, const QueueItemPtr> > Buffer;
	Buffer buffer;
	uint64_t now = GET_TICK();

	for(auto& q: pathQueue | map_values) {

		if(q->getSize() < PARTIAL_SHARE_MIN_SIZE) continue;

		const QueueItem::SourceList& sources = q->getSources();
		const QueueItem::SourceList& badSources = q->getBadSources();

		for(auto j = sources.begin(); j != sources.end(); ++j) {
			if(	(*j).isSet(QueueItem::Source::FLAG_PARTIAL) && (*j).getPartialSource()->getNextQueryTime() <= now &&
				(*j).getPartialSource()->getPendingQueryCount() < 10 && !(*j).getPartialSource()->getUdpPort().empty())
			{
				buffer.emplace((*j).getPartialSource()->getNextQueryTime(), make_pair(j, q));
			}
		}

		for(auto j = badSources.begin(); j != badSources.end(); ++j) {
			if(	(*j).isSet(QueueItem::Source::FLAG_TTH_INCONSISTENCY) == false && (*j).isSet(QueueItem::Source::FLAG_PARTIAL) &&
				(*j).getPartialSource()->getNextQueryTime() <= now && (*j).getPartialSource()->getPendingQueryCount() < 10 &&
				!(*j).getPartialSource()->getUdpPort().empty())
			{
				buffer.emplace((*j).getPartialSource()->getNextQueryTime(), make_pair(j, q));
			}
		}
	}

	// copy to results
	dcassert(sl.empty());
	const uint32_t maxElements = 10;
	sl.reserve(maxElements);
	for(auto i = buffer.begin(); i != buffer.end() && sl.size() < maxElements; i++){
		sl.push_back(i->second);
	}
}

} //dcpp
