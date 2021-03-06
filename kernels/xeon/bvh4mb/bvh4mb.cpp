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

#include "bvh4mb.h"
#include "geometry/triangle1v.h"
#include "common/accelinstance.h"

namespace embree
{
  DECLARE_SYMBOL(Accel::Intersector1,BVH4MBTriangle1vIntersector1Moeller);
  DECLARE_SYMBOL(Accel::Intersector4,BVH4MBTriangle1vIntersector4ChunkMoeller);
  DECLARE_SYMBOL(Accel::Intersector8,BVH4MBTriangle1vIntersector8ChunkMoeller);

  Builder* BVH4MBBuilderObjectSplit1 (void* bvh, BuildSource* source, void* geometry, const size_t minLeafSize, const size_t maxLeafSize);
  Builder* BVH4MBBuilderObjectSplit4 (void* bvh, BuildSource* source, void* geometry, const size_t minLeafSize, const size_t maxLeafSize);

  void BVH4MBRegister () 
  {
    int features = getCPUFeatures();

    SELECT_SYMBOL_DEFAULT_AVX_AVX2(features,BVH4MBTriangle1vIntersector1Moeller);
    SELECT_SYMBOL_DEFAULT_AVX_AVX2(features,BVH4MBTriangle1vIntersector4ChunkMoeller);
    SELECT_SYMBOL_AVX_AVX2(features,BVH4MBTriangle1vIntersector8ChunkMoeller);
  }

  Accel* BVH4MB::BVH4MBTriangle1v(Scene* scene)
  { 
    BVH4MB* accel = new BVH4MB(SceneTriangle1vMB::type,scene);

    Builder* builder = NULL;
    if      (g_builder == "default"     ) builder = BVH4MBBuilderObjectSplit1(accel,&scene->flat_triangle_source_2,scene,1,inf);
    else if (g_builder == "objectsplit" ) builder = BVH4MBBuilderObjectSplit1(accel,&scene->flat_triangle_source_2,scene,1,inf);
    else throw std::runtime_error("unknown builder "+g_builder+" for BVH4MB<Triangle1v>");
    
    Accel::Intersectors intersectors;
    intersectors.ptr = accel;
    intersectors.intersector1 = BVH4MBTriangle1vIntersector1Moeller;
    intersectors.intersector4 = BVH4MBTriangle1vIntersector4ChunkMoeller;
    intersectors.intersector8 = BVH4MBTriangle1vIntersector8ChunkMoeller;
    intersectors.intersector16 = NULL;

    return new AccelInstance(accel,builder,intersectors);
  }

  Accel* BVH4MB::BVH4MBTriangle1vObjectSplit(TriangleMeshScene::TriangleMesh* mesh)
  {
    BVH4MB* accel = new BVH4MB(TriangleMeshTriangle1vMB::type,mesh->parent);
    Builder* builder = BVH4MBBuilderObjectSplit1(accel,mesh,mesh,1,inf);

    Accel::Intersectors intersectors;
    intersectors.ptr = accel;
    intersectors.intersector1 = BVH4MBTriangle1vIntersector1Moeller;
    intersectors.intersector4 = BVH4MBTriangle1vIntersector4ChunkMoeller;
    intersectors.intersector8 = BVH4MBTriangle1vIntersector8ChunkMoeller;
    intersectors.intersector16 = NULL;

    return new AccelInstance(accel,builder,intersectors);
  }  

  void BVH4MB::clear() 
  {
    root = (Base*)Base::empty;
    bounds = empty;
    alloc.clear();
  }

   /*! Computes half surface area of box. */
  __forceinline float halfArea3f(const BBox<ssef>& box) {
    const ssef d = box.size();
    const ssef a = d*shuffle<1,2,0,3>(d);
    return a[0]+a[1]+a[2];
  }

  size_t BVH4MB::rotate(Base* nodeID, size_t depth)
  {
    /*! nothing to rotate if we reached a leaf node. */
    if (nodeID->isLeaf()) return 0;
    Node* parent = nodeID->node();

    /*! rotate all children first */
    ssei cdepth;
    for (size_t c=0; c<4; c++)
      cdepth[c] = (int)rotate(parent->child[c],depth+1);

    /* compute current area of all children */
    ssef sizeX = parent->upper_x-parent->lower_x;
    ssef sizeY = parent->upper_y-parent->lower_y;
    ssef sizeZ = parent->upper_z-parent->lower_z;
    ssef childArea = sizeX*(sizeY + sizeZ) + sizeY*sizeZ;

    /*! transpose node bounds */
    ssef plower0,plower1,plower2,plower3; transpose(parent->lower_x,parent->lower_y,parent->lower_z,ssef(zero),plower0,plower1,plower2,plower3);
    ssef pupper0,pupper1,pupper2,pupper3; transpose(parent->upper_x,parent->upper_y,parent->upper_z,ssef(zero),pupper0,pupper1,pupper2,pupper3);
    BBox<ssef> other0(plower0,pupper0), other1(plower1,pupper1), other2(plower2,pupper2), other3(plower3,pupper3);

    /*! Find best rotation. We pick a target child of a first child,
      and swap this with an other child. We perform the best such
      swap. */
    float bestCost = pos_inf;
    int bestChild = -1, bestTarget = -1, bestOther = -1;
    for (size_t c=0; c<4; c++)
    {
      /*! ignore leaf nodes as we cannot descent into */
      if (parent->child[c]->isLeaf()) continue;
      Node* child = parent->child[c]->node();

      /*! transpose child bounds */
      ssef clower0,clower1,clower2,clower3; transpose(child->lower_x,child->lower_y,child->lower_z,ssef(zero),clower0,clower1,clower2,clower3);
      ssef cupper0,cupper1,cupper2,cupper3; transpose(child->upper_x,child->upper_y,child->upper_z,ssef(zero),cupper0,cupper1,cupper2,cupper3);
      BBox<ssef> target0(clower0,cupper0), target1(clower1,cupper1), target2(clower2,cupper2), target3(clower3,cupper3);

      /*! put other0 at each target position */
      float cost00 = halfArea3f(merge(other0 ,target1,target2,target3));
      float cost01 = halfArea3f(merge(target0,other0 ,target2,target3));
      float cost02 = halfArea3f(merge(target0,target1,other0 ,target3));
      float cost03 = halfArea3f(merge(target0,target1,target2,other0 ));
      ssef cost0 = ssef(cost00,cost01,cost02,cost03);
      ssef min0 = vreduce_min(cost0);
      int pos0 = (int)__bsf(movemask(min0 == cost0));

      /*! put other1 at each target position */
      float cost10 = halfArea3f(merge(other1 ,target1,target2,target3));
      float cost11 = halfArea3f(merge(target0,other1 ,target2,target3));
      float cost12 = halfArea3f(merge(target0,target1,other1 ,target3));
      float cost13 = halfArea3f(merge(target0,target1,target2,other1 ));
      ssef cost1 = ssef(cost10,cost11,cost12,cost13);
      ssef min1 = vreduce_min(cost1);
      int pos1 = (int)__bsf(movemask(min1 == cost1));

      /*! put other2 at each target position */
      float cost20 = halfArea3f(merge(other2 ,target1,target2,target3));
      float cost21 = halfArea3f(merge(target0,other2 ,target2,target3));
      float cost22 = halfArea3f(merge(target0,target1,other2 ,target3));
      float cost23 = halfArea3f(merge(target0,target1,target2,other2 ));
      ssef cost2 = ssef(cost20,cost21,cost22,cost23);
      ssef min2 = vreduce_min(cost2);
      int pos2 = (int)__bsf(movemask(min2 == cost2));

      /*! put other3 at each target position */
      float cost30 = halfArea3f(merge(other3 ,target1,target2,target3));
      float cost31 = halfArea3f(merge(target0,other3 ,target2,target3));
      float cost32 = halfArea3f(merge(target0,target1,other3 ,target3));
      float cost33 = halfArea3f(merge(target0,target1,target2,other3 ));
      ssef cost3 = ssef(cost30,cost31,cost32,cost33);
      ssef min3 = vreduce_min(cost3);
      int pos3 = (int)__bsf(movemask(min3 == cost3));

      /*! find best other child */
      ssef otherCost = ssef(extract<0>(min0),extract<0>(min1),extract<0>(min2),extract<0>(min3));
      int pos[4] = { pos0,pos1,pos2,pos3 };
      sseb valid = ssei(int(depth+1))+cdepth <= ssei(maxDepth); // only select swaps that fulfill depth constraints
      if (none(valid)) continue;
      
      size_t n = select_min(valid,otherCost);
      float cost = otherCost[n]-childArea[c]; //< increasing the original child bound is bad, decreasing good

      /*! accept a swap when it reduces cost and is not swapping a node with itself */
      if (cost < bestCost && n != c) {
        bestCost = cost;
        bestChild = (int)c;
        bestOther = (int)n;
        bestTarget = pos[n];
      }
    }

    /*! if we did not find a swap that improves the SAH then do nothing */
    if (bestCost >= 0) return 1+reduce_max(cdepth);

    /*! perform the best found tree rotation */
    Node* child = parent->child[bestChild]->node();
    swap(parent,bestOther,child,bestTarget);
    parent->lower_x[bestChild] = reduce_min(child->lower_x);
    parent->lower_y[bestChild] = reduce_min(child->lower_y);
    parent->lower_z[bestChild] = reduce_min(child->lower_z);
    parent->upper_x[bestChild] = reduce_max(child->upper_x);
    parent->upper_y[bestChild] = reduce_max(child->upper_y);
    parent->upper_z[bestChild] = reduce_max(child->upper_z);
    parent->lower_dx[bestChild] = reduce_min(child->lower_dx);
    parent->lower_dy[bestChild] = reduce_min(child->lower_dy);
    parent->lower_dz[bestChild] = reduce_min(child->lower_dz);
    parent->upper_dx[bestChild] = reduce_max(child->upper_dx);
    parent->upper_dy[bestChild] = reduce_max(child->upper_dy);
    parent->upper_dz[bestChild] = reduce_max(child->upper_dz);

    /*! This returned depth is conservative as the child that was
     *  pulled up in the tree could have been on the critical path. */
    cdepth[bestOther]++; // bestOther was pushed down one level
    return 1+reduce_max(cdepth); 
  }

  std::pair<BBox3f,BBox3f> BVH4MB::refit(void* geom, Base* node)
  {
    /*! merge bounds of triangles for both time steps */
    if (node->isLeaf()) 
    {
      size_t num; char* tri = node->leaf(num);
      return primTy.update2(tri,num,geom);
    }
    /*! set and propagate merged bounds for both time steps */
    else
    {
      Node* n = node->node();
      if (!n->hasBounds()) {
        for (size_t i=0; i<4; i++) {
          std::pair<BBox3f,BBox3f> bounds = refit(geom,n->child[i]);
          n->set(i,bounds.first,bounds.second);
        }
      }
      BBox3f bounds0 = merge(n->bounds0(0),n->bounds0(1),n->bounds0(2),n->bounds0(3));
      BBox3f bounds1 = merge(n->bounds1(0),n->bounds1(1),n->bounds1(2),n->bounds1(3));
      return std::pair<BBox3f,BBox3f>(bounds0,bounds1);
    }
  }

  float BVH4MB::statistics(Base* node, float ap, size_t& depth)
  {
    if (node->isNode())
    {
      numNodes++;
      depth = 0;
      size_t cdepth = 0;
      const Node* n = node->node();
      const ssef dx = n->upper_x-n->lower_x;
      const ssef dy = n->upper_y-n->lower_y;
      const ssef dz = n->upper_z-n->lower_z;
      const ssef ac = 2.0f*(dx*(dy+dz)+dy*dz);
      const float sah0 = statistics(n->child[0],ac[0],cdepth); depth=max(depth,cdepth);
      const float sah1 = statistics(n->child[1],ac[1],cdepth); depth=max(depth,cdepth);
      const float sah2 = statistics(n->child[2],ac[2],cdepth); depth=max(depth,cdepth);
      const float sah3 = statistics(n->child[3],ac[3],cdepth); depth=max(depth,cdepth);
      depth++;
      return ap*travCost + sah0 + sah1 + sah2 + sah3;
    }
    else
    {
      depth = 0;
      size_t num; char* tri = node->leaf(num);
      if (!num) return 0.0f;

      numLeaves++;
      numPrimBlocks += num;
      for (size_t i=0; i<num; i++)
        numPrims += primTy.size(tri+i*primTy.bytes);
      return primTy.intCost * ap * num;
    }
  }

  void BVH4MB::print() 
  {
    /* calculate statistics */
    numNodes = numLeaves = numPrimBlocks = numPrims = depth = 0;
    bvhSAH = statistics(root,0.0f,depth);

    /* output statistics */
    std::ostringstream stream;
    size_t bytesNodes = numNodes     *sizeof(Node);
    size_t bytesTris  = numPrimBlocks*primTy.bytes;
    size_t bytesVertices = 0; //numVertices*sizeof(Vec3fa);
    size_t bytesTotal = bytesNodes+bytesTris+bytesVertices;
    stream.setf(std::ios::scientific, std::ios::floatfield);
    stream.precision(2);
    stream << "sah = " << bvhSAH << std::endl;
    stream.setf(std::ios::fixed, std::ios::floatfield);
    stream.precision(1);
    stream << "depth = " << depth << std::endl;
    stream << "size = " << bytesTotal/1E6 << " MB" << std::endl;
    stream << "nodes = "  << numNodes << " "
         << "(" << bytesNodes/1E6  << " MB) "
         << "(" << 100.0*double(bytesNodes)/double(bytesTotal) << "% of total) "
         << "(" << 100.0*(numNodes-1+numLeaves)/(4.0*numNodes) << "% used)" 
         << std::endl;
    stream << "leaves = " << numLeaves << " "
         << "(" << bytesTris/1E6  << " MB) "
         << "(" << 100.0*double(bytesTris)/double(bytesTotal) << "% of total) "
         << "(" << 100.0*numPrims/(primTy.blockSize*numPrimBlocks) << "% used)" 
         << std::endl;
    /*stream << "vertices = " << numVertices << " "
         << "(" << bytesVertices/1E6 << " MB) " 
         << "(" << 100.0*double(bytesVertices)/double(bytesTotal) << "% of total) "
         << "(" << 100.0*12.0f/float(sizeof(Vec3fa)) << "% used)" 
         << std::endl;*/
    std::cout << stream.str();
  }
}
