// ======================================================================== //
// Copyright 2009-2013 Intel Corporation                                    //
//                                                                          //
// Licensed under the Apache License, Version 2.0 (the "License");          //
// you may not use this file except in compliance with the License.         //
// You may obtain a copy of the License at                                  //
//                                                                          //
//     http://www.apache.org/licenses/LICENSE-2.0                           //
//                                                                          //
// Unless required by applicable law or agreed to in writing, software      //
// distributed under the License is distributed on an "AS IS" BASIS,        //
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. //
// See the License for the specific language governing permissions and      //
// limitations under the License.                                           //
// ======================================================================== //

#ifndef __EMBREE_BVHI_BUILDER_MORTON_MIC_H__
#define __EMBREE_BVHI_BUILDER_MORTON_MIC_H__

#include "bvh4i.h"
#include "bvh4i/bvh4i_builder_util.h"
#include "bvh4i/bvh4i_builder_util_mic.h"
#include "bvh4i_statistics.h"

namespace embree
{
  class BVH4iBuilderMorton : public Builder
  {
    ALIGNED_CLASS;

    enum { RECURSE = 1, CREATE_TOP_LEVEL = 2 };

    static const size_t GLOBAL_WORK_STACK_ENTRIES = 512;
    static const size_t MAX_TOP_LEVEL_BINS = 1024;
    static const size_t ALLOCATOR_NODE_BLOCK_SIZE = 64;
    static const size_t MORTON_LEAF_THRESHOLD = 4;
    static const size_t LATTICE_BITS_PER_DIM = 10;
    static const size_t LATTICE_SIZE_PER_DIM = size_t(1) << LATTICE_BITS_PER_DIM;
    typedef AtomicIDBlock<ALLOCATOR_NODE_BLOCK_SIZE> NodeAllocator;

  public:

    class __align(16) SmallBuildRecord 
    {
    public:
      unsigned int begin;
      unsigned int end;
      unsigned int depth;
      unsigned int parentID;
      
      __forceinline unsigned int size() const {
        return end - begin;
      }
      
      __forceinline void init(const unsigned int _begin, const unsigned int _end)			 
      {
        begin = _begin;
        end = _end;
        depth = 1;
        parentID = 0;
      }
      
      __forceinline bool operator<(const SmallBuildRecord& br) const { return size() < br.size(); } 
      __forceinline bool operator>(const SmallBuildRecord& br) const { return size() > br.size(); } 

    __forceinline friend std::ostream &operator<<(std::ostream &o, const SmallBuildRecord &br)
      {
	o << "begin " << br.begin << " end " << br.end << " size " << br.size() << " depth " << br.depth << " parentID " << br.parentID;
	return o;
      }

    };

    

    struct __align(8) MortonID32Bit
    {
      unsigned int code;
      unsigned int index;
            
      __forceinline unsigned int get(const unsigned int shift, const unsigned and_mask) const {
        return (code >> shift) & and_mask;
      }

      __forceinline unsigned int getByte(const size_t b) const {
	assert(b < 4);
	const unsigned char *__restrict const ptr = (const unsigned char*)&code;
	return ptr[b];
      }
      
      __forceinline void operator=(const MortonID32Bit& v) {
        *(size_t*)this = *(size_t*)&v;
      };
      
      __forceinline friend std::ostream &operator<<(std::ostream &o, const MortonID32Bit& mc) {
        o << "index " << mc.index << " code = " << mc.code;
        return o;
      }

      __forceinline bool operator<(const MortonID32Bit &m) const { return code < m.code; } 
      __forceinline bool operator>(const MortonID32Bit &m) const { return code > m.code; } 
    };

    /*! Constructor. */
    BVH4iBuilderMorton (BVH4i* bvh, BuildSource* source, void* geometry);

    /*! Destructor. */
    ~BVH4iBuilderMorton();

    /*! creates the builder */
    static Builder* create (void* accel, BuildSource* source, void* geometry) { 
      return new BVH4iBuilderMorton((BVH4i*)accel,source,geometry);
    }

    /* build function */
    void build(size_t threadIndex, size_t threadCount);

    /*! initialized the builder */
    void initEncodingAllocateData(size_t threadCount);

    /*! precalculate some per thread data */
    void initThreadState(const size_t threadID, const size_t numThreads);

    /*! main build task */
    TASK_RUN_FUNCTION(BVH4iBuilderMorton,build_parallel_morton);
    TaskScheduler::Task task;
    
    /*! task that calculates the bounding box of the scene */
    TASK_FUNCTION(BVH4iBuilderMorton,computeBounds);

    /*! task that calculates the morton codes for each primitive in the scene */
    TASK_FUNCTION(BVH4iBuilderMorton,computeMortonCodes);
    
    /*! parallel sort of the morton codes */
    TASK_FUNCTION(BVH4iBuilderMorton,radixsort);

    /*! builds top of the tree in parallel */
    TASK_FUNCTION(BVH4iBuilderMorton,createTopLevelTree);

    /*! task that builds a list of sub-trees */
    TASK_FUNCTION(BVH4iBuilderMorton,recurseSubMortonTrees);

    /*! task that converts the BVH layout to SOA */
    TASK_FUNCTION(BVH4iBuilderMorton,convertToSOALayout);

  public:

    void build_main(const size_t threadID, const size_t numThreads);

    /*! creates a leaf node */
    BBox3f createSmallLeaf(SmallBuildRecord& current) const;


    BBox3f createLeaf(SmallBuildRecord& current, NodeAllocator& alloc);

    /*! fallback split mode */
    void split_fallback(SmallBuildRecord& current, SmallBuildRecord& leftChild, SmallBuildRecord& rightChild) const;

    /*! split a build record into two */
    bool split(SmallBuildRecord& current, SmallBuildRecord& left, SmallBuildRecord& right) const;

    /*! create the top-levels of the tree */
    size_t createQBVHNode(SmallBuildRecord& current, SmallBuildRecord *__restrict__ const children);

    /*! main recursive build function */
    BBox3f recurse(SmallBuildRecord& current, 
		   NodeAllocator& alloc,
		   const size_t mode, 
		   const size_t numThreads);
    
    /*! refit the toplevel part of the BVH */
    void refit_toplevel(const size_t index) const;

    /*! refit the sub-BVHs */
    void refit(const size_t index) const;
    
    /*! recreates morton codes when reaching a region where all codes are identical */
    void recreateMortonCodes(SmallBuildRecord& current) const;

  public:
    BVH4i      * bvh;         //!< Output BVH
    BuildSource* source;      //!< input geometry
    Scene      * scene;

    size_t topLevelItemThreshold;
    size_t numBuildRecords;
    unsigned int encodeShift;
    unsigned int encodeMask;

    __align(64) LinearBarrierActive barrier;
    __align(64) SmallBuildRecord buildRecords[MAX_TOP_LEVEL_BINS];    
    __align(64) unsigned int thread_startGroup[MAX_MIC_THREADS];      
    __align(64) unsigned int thread_startGroupOffset[MAX_MIC_THREADS];


    /*! state for radix sort */
  public:
    static const size_t RADIX_BITS = 8;
    static const size_t RADIX_BUCKETS = (1 << RADIX_BITS);
    static const size_t RADIX_BUCKETS_MASK = (RADIX_BUCKETS-1);
    __align(64) unsigned int radixCount[MAX_MIC_THREADS][RADIX_BUCKETS];

  protected:
    MortonID32Bit* __restrict__ morton;
    BVHNode      * __restrict__ node;
    Triangle1    * __restrict__ accel;

    size_t numGroups;
    size_t numPrimitives;
    size_t numNodes;
    size_t numAllocatedNodes;
    size_t size_morton;

    __align(64) Centroid_Scene_AABB global_bounds;

    /*! node allocator */
    __align(64) AlignedAtomicCounter32  atomicID;
    __align(64) AlignedAtomicCounter32  numBuildRecordCounter;

    __forceinline unsigned int allocNode(int size)
    {
      const unsigned int currentIndex = atomicID.add(size);
      if (unlikely(currentIndex >= numAllocatedNodes)) {
        FATAL("not enough nodes allocated");
      }
      return currentIndex;
    }

  };

}

#endif
