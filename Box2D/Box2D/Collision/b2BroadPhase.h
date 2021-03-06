/*
* Copyright (c) 2006-2009 Erin Catto http://www.box2d.org
* Copyright (c) 2015, Justin Hoffman https://github.com/skitzoid
*
* This software is provided 'as-is', without any express or implied
* warranty.  In no event will the authors be held liable for any damages
* arising from the use of this software.
* Permission is granted to anyone to use this software for any purpose,
* including commercial applications, and to alter it and redistribute it
* freely, subject to the following restrictions:
* 1. The origin of this software must not be misrepresented; you must not
* claim that you wrote the original software. If you use this software
* in a product, an acknowledgment in the product documentation would be
* appreciated but is not required.
* 2. Altered source versions must be plainly marked as such, and must not be
* misrepresented as being the original software.
* 3. This notice may not be removed or altered from any source distribution.
*/

#ifndef B2_BROAD_PHASE_H
#define B2_BROAD_PHASE_H

#include <Box2D/Common/b2Settings.h>
#include <Box2D/Collision/b2Collision.h>
#include <Box2D/Collision/b2DynamicTree.h>
#include <Box2D/Common/b2GrowableArray.h>
#include <algorithm>

struct b2Pair
{
	int32 proxyIdA;
	int32 proxyIdB;
};

struct b2BroadPhasePerThreadData
{
	b2GrowableArray<b2Pair> m_pairBuffer;
	b2GrowableArray<int32> m_moveBuffer;
	int32 m_queryProxyId;

	uint8 m_padding[b2_cacheLineSize];
};

/// The broad-phase is used for computing pairs and performing volume queries and ray casts.
/// This broad-phase does not persist pairs. Instead, this reports potentially new pairs.
/// It is up to the client to consume the new pairs and to track subsequent overlap.
class b2BroadPhase
{
public:

	enum
	{
		e_nullProxy = -1
	};

	b2BroadPhase();
	~b2BroadPhase();

	/// Create a proxy with an initial AABB. Pairs are not reported until
	/// UpdatePairs is called.
	int32 CreateProxy(const b2AABB& aabb, void* userData);

	/// Destroy a proxy. It is up to the client to remove any pairs.
	void DestroyProxy(int32 proxyId);

	/// Call MoveProxy as many times as you like, then when you are done
	/// call UpdatePairs to finalized the proxy pairs (for your time step).
	void MoveProxy(int32 proxyId, const b2AABB& aabb, const b2Vec2& displacement);

	/// Call to trigger a re-processing of it's pairs on the next call to UpdatePairs.
	void TouchProxy(int32 proxyId);

	/// Get the fat AABB for a proxy.
	const b2AABB& GetFatAABB(int32 proxyId) const;

	/// Get user data from a proxy. Returns NULL if the id is invalid.
	void* GetUserData(int32 proxyId) const;

	/// Test overlap of fat AABBs.
	bool TestOverlap(int32 proxyIdA, int32 proxyIdB) const;

	/// Get the number of proxies.
	int32 GetProxyCount() const;

	/// Update the pairs. This results in pair callbacks. This can only add pairs.
	template <typename T>
	void UpdatePairs(int32 moveBegin, int32 moveEnd, T* callback);

	/// Query an AABB for overlapping proxies. The callback class
	/// is called for each proxy that overlaps the supplied AABB.
	template <typename T>
	void Query(T* callback, const b2AABB& aabb) const;

	/// Ray-cast against the proxies in the tree. This relies on the callback
	/// to perform a exact ray-cast in the case were the proxy contains a shape.
	/// The callback also performs the any collision filtering. This has performance
	/// roughly equal to k * log(n), where k is the number of collisions and n is the
	/// number of proxies in the tree.
	/// @param input the ray-cast input data. The ray extends from p1 to p1 + maxFraction * (p2 - p1).
	/// @param callback a callback class that is called for each proxy that is hit by the ray.
	template <typename T>
	void RayCast(T* callback, const b2RayCastInput& input) const;

	/// Get the height of the embedded tree.
	int32 GetTreeHeight() const;

	/// Get the balance of the embedded tree.
	int32 GetTreeBalance() const;

	/// Get the quality metric of the embedded tree.
	float32 GetTreeQuality() const;

	/// Shift the world origin. Useful for large worlds.
	/// The shift formula is: position -= newOrigin
	/// @param newOrigin the new origin with respect to the old origin
	void ShiftOrigin(const b2Vec2& newOrigin);

	/// Reset the move buffer. Should only be called by the multi-threaded contact finder.
	void ResetMoveBuffer();

	/// Get the number of proxies in the move buffer.
	int32 GetMoveCount() const;

private:

	friend class b2DynamicTree;

	void BufferMove(int32 proxyId);
	void UnBufferMove(int32 proxyId);

	bool QueryCallback(int32 proxyId);

	b2DynamicTree m_tree;

	int32 m_proxyCount;

	b2BroadPhasePerThreadData m_perThreadData[b2_maxThreads];
};

/// This is used to sort pairs.
inline bool b2PairLessThan(const b2Pair& pair1, const b2Pair& pair2)
{
	if (pair1.proxyIdA < pair2.proxyIdA)
	{
		return true;
	}

	if (pair1.proxyIdA == pair2.proxyIdA)
	{
		return pair1.proxyIdB < pair2.proxyIdB;
	}

	return false;
}

inline void* b2BroadPhase::GetUserData(int32 proxyId) const
{
	return m_tree.GetUserData(proxyId);
}

inline bool b2BroadPhase::TestOverlap(int32 proxyIdA, int32 proxyIdB) const
{
	const b2AABB& aabbA = m_tree.GetFatAABB(proxyIdA);
	const b2AABB& aabbB = m_tree.GetFatAABB(proxyIdB);
	return b2TestOverlap(aabbA, aabbB);
}

inline const b2AABB& b2BroadPhase::GetFatAABB(int32 proxyId) const
{
	return m_tree.GetFatAABB(proxyId);
}

inline int32 b2BroadPhase::GetProxyCount() const
{
	return m_proxyCount;
}

inline int32 b2BroadPhase::GetTreeHeight() const
{
	return m_tree.GetHeight();
}

inline int32 b2BroadPhase::GetTreeBalance() const
{
	return m_tree.GetMaxBalance();
}

inline float32 b2BroadPhase::GetTreeQuality() const
{
	return m_tree.GetAreaRatio();
}

template <typename T>
void b2BroadPhase::UpdatePairs(int32 moveBegin, int32 moveEnd, T* callback)
{
	int32 threadId = b2GetThreadId();

	b2BroadPhasePerThreadData* td = m_perThreadData + threadId;

	// Reset pair buffer
	td->m_pairBuffer.Clear();

	// Perform tree queries for all moving proxies.
	for (int32 i = moveBegin; i < moveEnd; ++i)
	{
		td->m_queryProxyId = m_perThreadData[0].m_moveBuffer.At(i);
		if (td->m_queryProxyId == e_nullProxy)
		{
			continue;
		}

		b2AABB fatAABB;

		// We have to query the tree with the fat AABB so that
		// we don't fail to create a pair that may touch later.
		fatAABB = m_tree.GetFatAABB(td->m_queryProxyId);

		// Query the tree, create pairs and add them pair buffer.
		m_tree.Query(this, fatAABB);
	}

	// Reset move buffer if we're processing the entire range.
	if (moveBegin == 0 && moveEnd == m_perThreadData[0].m_moveBuffer.GetCount())
	{
		m_perThreadData[0].m_moveBuffer.Clear();
	}

	// Sort the pair buffer to expose duplicates.
	std::sort(td->m_pairBuffer.Data(), td->m_pairBuffer.Data() + td->m_pairBuffer.GetCount(), b2PairLessThan);

	// Send the pairs back to the client.
	int32 i = 0;
	while (i < td->m_pairBuffer.GetCount())
	{
		b2Pair& primaryPair = td->m_pairBuffer.At(i);

		void* userDataA = m_tree.GetUserData(primaryPair.proxyIdA);
		void* userDataB = m_tree.GetUserData(primaryPair.proxyIdB);

		callback->AddPair(userDataA, userDataB);

		++i;

		// Skip any duplicate pairs.
		while (i < td->m_pairBuffer.GetCount())
		{
			b2Pair& pair = td->m_pairBuffer.At(i);
			if (pair.proxyIdA != primaryPair.proxyIdA || pair.proxyIdB != primaryPair.proxyIdB)
			{
				break;
			}
			++i;
		}
	}

	// Try to keep the tree balanced.
	//m_tree.Rebalance(4);
}

template <typename T>
inline void b2BroadPhase::Query(T* callback, const b2AABB& aabb) const
{
	m_tree.Query(callback, aabb);
}

template <typename T>
inline void b2BroadPhase::RayCast(T* callback, const b2RayCastInput& input) const
{
	m_tree.RayCast(callback, input);
}

inline void b2BroadPhase::ShiftOrigin(const b2Vec2& newOrigin)
{
	m_tree.ShiftOrigin(newOrigin);
}

inline void b2BroadPhase::ResetMoveBuffer()
{
	for (int32 i = 0; i < b2_maxThreads; ++i)
	{
		m_perThreadData[i].m_moveBuffer.Clear();
	}
}

inline int32 b2BroadPhase::GetMoveCount() const
{
	int32 moveCount = 0;
	for (int32 i = 0; i < b2_maxThreads; ++i)
	{
		moveCount += m_perThreadData[i].m_moveBuffer.GetCount();
	}
	return moveCount;
}

#endif
