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

#ifndef __EMBREE_INSTANCE_INTERSECTOR8_H__
#define __EMBREE_INSTANCE_INTERSECTOR8_H__

#include "common/scene_user_geometry.h"
#include "common/ray8.h"

namespace embree
{
  namespace isa
  {
    struct FastInstanceIntersector8
    {
      static void intersect(avxb* valid, const UserGeometryScene::Instance* instance, Ray8& ray, size_t item);
      static void occluded (avxb* valid, const UserGeometryScene::Instance* instance, Ray8& ray, size_t item);
    };
  }
}

#endif
  
