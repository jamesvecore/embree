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

#ifndef __EMBREE_BVH8I_INTERSECTOR1_H__
#define __EMBREE_BVH8I_INTERSECTOR1_H__

#include "bvh8i.h"
#include "../common/stack_item.h"
#include "../common/ray.h"

namespace embree
{
  namespace isa
  {
    /*! BVH8i Traverser. Single ray traversal implementation for a 8-wide BVH. */

    template<typename TriangleIntersector>
    class BVH8iIntersector1
    {

      typedef typename TriangleIntersector::Primitive Triangle;
      typedef typename BVH4i::NodeRef NodeRef;
      typedef typename BVH8i::Node Node;
      typedef StackItemT<unsigned> StackItem;

    public:


      static void intersect(const BVH8i* This, Ray& ray);
      static void occluded (const BVH8i* This, Ray& ray);
    };
  }
}

#endif
  
