/*
 * LRUCache11 - a templated C++11 based LRU cache class that allows
 * specification of
 * key, value and optionally the map container type (defaults to
 * std::unordered_map)
 * By using the std::unordered_map and a linked list of keys it allows O(1) insert, delete
 * and
 * refresh operations.
 *
 * This is a header-only library and all you need is the LRUCache11.hpp file
 *
 * Github: https://github.com/mohaps/lrucache11
 *
 * This is a follow-up to the LRUCache project -
 * https://github.com/mohaps/lrucache
 *
 * Copyright (c) 2012-22 SAURAV MOHAPATRA <mohaps@gmail.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#pragma once
#include <algorithm>
#include <bitset>
#include <cstdint>
#include <list>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#ifndef MAX_BUCKET_SIZE
#define MAX_BUCKET_SIZE 1741 // /usr/include/c++/9/ext/pb_ds/detail/resize_policy/hash_prime_size_policy_imp.hpp
#endif

#ifndef MAX_CACHE_ENTRY
#define MAX_CACHE_ENTRY 1741
#endif

namespace lru11 {
/*
 * a noop lockable concept that can be used in place of std::mutex
 */
class NullLock {
public:
	void lock() {}
	void unlock() {}
	bool try_lock() { return true; }
};

/**
 * error raised when a key not in cache is passed to get()
 */
class KeyNotFound : public std::invalid_argument {
public:
	KeyNotFound() :
			std::invalid_argument("key_not_found") {}
};

template <typename K, typename V>
struct KeyValuePair {
public:
	K key;
	V value;

	KeyValuePair(K k, V v) :
			key(std::move(k)), value(std::move(v)) {}
};

/**
 *	The LRU Cache class templated by
 *		Key - key type
 *		Value - value type
 *		MapType - an associative container like std::unordered_map
 *		LockType - a lock type derived from the Lock class (default:
 *NullLock = no synchronization)
 *
 *	The default NullLock based template is not thread-safe, however passing
 *Lock=std::mutex will make it
 *	thread-safe
 */
template <class Key, class Value, class Lock = NullLock,
		class Map = std::unordered_map<
				Key, typename std::list<KeyValuePair<Key, Value> >::iterator> >
class Cache {
public:
	typedef KeyValuePair<Key, Value> node_type;
	typedef std::list<KeyValuePair<Key, Value> > list_type;
	typedef Map map_type;
	typedef Lock lock_type;
	using Guard = std::lock_guard<lock_type>;
	std::string cache_name;
	/**
   * the maxSize is the soft limit of keys and (maxSize + elasticity) is the
   * hard limit
   * the cache is allowed to grow till (maxSize + elasticity) and is pruned back
   * to maxSize keys
   * set maxSize = 0 for an unbounded cache (but in that case, you're better off
   * using a std::unordered_map
   * directly anyway! :)
   */
	explicit Cache(std::string name, size_t maxSize = 64, size_t elasticity = 10) :
			maxSize_(maxSize), elasticity_(elasticity) {
		cache_name = name;
		cache_.reserve(MAX_BUCKET_SIZE);
	}
	virtual ~Cache() = default;
	size_t size() const {
		Guard g(lock_);
		return cache_.size();
	}
	bool empty() const {
		Guard g(lock_);
		return cache_.empty();
	}
	void clear() {
		Guard g(lock_);
		cache_.clear();
		keys_.clear();
	}
	float get_loadfactor() {
		return cache_.load_factor();
	}
	bool insert(const Key &k, Value v) {
		Guard g(lock_);
		const auto iter = cache_.find(k);
		if (iter != cache_.end()) {
			iter->second->value = v;
			keys_.splice(keys_.begin(), keys_, iter->second);
			return false;
		}

		keys_.emplace_front(k, std::move(v));
		cache_[k] = keys_.begin();
		prune();
		return true;
	}
	std::bitset<24> create_ccache_field(std::bitset<20> bucket_id, std::bitset<4> index) {
		std::bitset<24> merged(bucket_id.to_string() + index.to_string());
		std::bitset<24> ccache_field{ merged };
		return ccache_field;
	}
	void get_locator(const Key &k, void *locator) {
		Guard g(lock_);
		std::size_t bucket_id = cache_.bucket(k);
		std::size_t target_id;
		int index = 0;
		for (auto iter = cache_.begin(bucket_id); iter != cache_.end(bucket_id); iter++) {
			if (iter->second->key == k)
				break;
			else
				index++;
		}
		std::bitset<20> bucket_bit(bucket_id);
		// std::cout << bucket_id << "\t" << bucket_bit << std::endl;

		std::bitset<4> index_bit(index);
		std::bitset<24> ccache_bit = create_ccache_field(bucket_bit, index_bit);
		// std::cout << bucket_id << "\t" << index << std::endl;
		memcpy(locator, &ccache_bit, 3); //3byte

		// return std::make_pair<std::size_t, int>(bucket_id, index);
	}
	const Value &direct_find(std::size_t bucket_id, int index) {
		Guard g(lock_);
		int i = 0;

		for (auto iter = cache_.begin(bucket_id); iter != cache_.end(bucket_id); iter++) {
			if (i == index)
				return iter->second->value;
			i++;
		}
		throw KeyNotFound();
	}
	bool tryFind(const Key &k) {
		Guard g(lock_);
		const auto iter = cache_.find(k);
		if (iter == cache_.end()) {
			return true;
		}
		return false;
	}
	bool tryGet(const Key &kIn, Value &vOut) {
		Guard g(lock_);
		const auto iter = cache_.find(kIn);
		if (iter == cache_.end()) {
			return false;
		}
		keys_.splice(keys_.begin(), keys_, iter->second);
		vOut = iter->second->value;
		return true;
	}
	void update(const Key &k) {
		const auto iter = cache_.find(k);
		keys_.splice(keys_.begin(), keys_, iter->second);
	}
	/**
   *	The const reference returned here is only
   *    guaranteed to be valid till the next insert/delete
   */
	const Value &get(const Key &k) {
		Guard g(lock_);
		const auto iter = cache_.find(k);
		if (iter == cache_.end()) {
			throw KeyNotFound();
		}
		keys_.splice(keys_.begin(), keys_, iter->second);
		return iter->second->value;
	}
	/**
   * returns a copy of the stored object (if found)
   */
	Value getCopy(const Key &k) {
		return get(k);
	}
	bool remove(const Key &k) {
		Guard g(lock_);
		auto iter = cache_.find(k);
		if (iter == cache_.end()) {
			return false;
		}
		keys_.erase(iter->second);
		cache_.erase(iter);
		return true;
	}
	bool contains(const Key &k) const {
		Guard g(lock_);
		return cache_.find(k) != cache_.end();
	}
	size_t getMaxSize() const { return maxSize_; }
	size_t getElasticity() const { return elasticity_; }
	size_t getMaxAllowedSize() const { return maxSize_ + elasticity_; }
	template <typename F>
	void cwalk(F &f) const {
		Guard g(lock_);
		std::for_each(keys_.begin(), keys_.end(), f);
	}

protected:
	size_t prune() {
		size_t maxAllowed = maxSize_ + elasticity_;
		if (maxSize_ == 0 || cache_.size() < maxAllowed) {
			return 0;
		}
		size_t count = 0;
		while (cache_.size() > maxSize_) {
			// std::cout << keys_.back().key << std::endl;
			cache_.erase(keys_.back().key);
			keys_.pop_back();
			++count;
		}
		return count;
	}

private:
	// Disallow copying.
	Cache(const Cache &) = delete;
	Cache &operator=(const Cache &) = delete;

	mutable Lock lock_;
	Map cache_;
	list_type keys_;
	size_t maxSize_;
	size_t elasticity_;
};

} // namespace lru11