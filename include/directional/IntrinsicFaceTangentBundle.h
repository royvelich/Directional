// This file is part of Directional, a library for directional field processing.
// Copyright (C) 2022 Amir Vaxman <avaxman@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.

#ifndef DIRECTIONAL_INTRINSIC_FACE_TANGENT_BUNDLE_H
#define DIRECTIONAL_INTRINSIC_FACE_TANGENT_BUNDLE_H

#include <iostream>
#include <Eigen/Geometry>
#include <Eigen/Sparse>
#include <igl/boundary_loop.h>
#include <igl/doublearea.h>
#include <directional/dual_cycles.h>
#include <directional/TriMesh.h>


namespace directional{

//This is the interface class for any directional fields represented in cartesian coordinates, of any order N.
class IntrinsicFaceTangentBundle : public TangentBundle{
public:
  
  const TriMesh* mesh;

  discTangTypeEnum discTangType() const {return discTangTypeEnum::FACE_SPACES;}

  bool hasCochainSequence() const { return true; }
  
  IntrinsicFaceTangentBundle(){}
  ~IntrinsicFaceTangentBundle(){}
  
  void IGL_INLINE init(const TriMesh& _mesh){
    
    typedef std::complex<double> Complex;
    mesh = &_mesh;
    
    //adjacency relation is by dual edges.
    adjSpaces = mesh->EF;
    oneRing = mesh->FE;
    sources = mesh->barycenters;
    normals = mesh->faceNormals;
    cycleSources = mesh->V;
    cycleNormals = mesh->vertexNormals;
    
    //connection is the ratio of the complex representation of edges
    connection.resize(mesh->EF.rows(),1);  //the difference in the angle representation of edge i from EF(i,0) to EF(i,1)
    Eigen::MatrixXd edgeVectors(mesh->EF.rows(), 3);
    for (int i = 0; i < mesh->EF.rows(); i++) {
      if (mesh->EF(i, 0) == -1 || mesh->EF(i, 1) == -1)
        continue;
      edgeVectors.row(i) = (mesh->V.row(mesh->EV(i, 1)) - mesh->V.row(mesh->EV(i, 0))).normalized();
      Complex ef(edgeVectors.row(i).dot(mesh->FBx.row(mesh->EF(i, 0))), edgeVectors.row(i).dot(mesh->FBy.row(mesh->EF(i, 0))));
      Complex eg(edgeVectors.row(i).dot(mesh->FBx.row(mesh->EF(i, 1))), edgeVectors.row(i).dot(mesh->FBy.row(mesh->EF(i, 1))));
      connection(i) = eg / ef;
    }
    
    //TODO: cycles, cycleCurvature
    directional::dual_cycles(mesh->V, mesh->F, mesh->EV, mesh->EF, cycles, cycleCurvatures, local2Cycle, innerAdjacencies);
    
    //drawing from mesh geometry
    
    /************Smoothness matrices****************/
    stiffnessWeights=Eigen::VectorXd::Zero(mesh->EF.rows());
   
    //mass are face areas
    igl::doublearea(mesh->V,mesh->F,massWeights);
    massWeights.array()/=2.0;

    //The "harmonic" weights from [Brandt et al. 2020].
    for (int i=0;i<mesh->EF.rows();i++){
      if ((mesh->EF(i,0)==-1)||(mesh->EF(i,1)==-1))
        continue;  //boundary edge
      
      double primalLengthSquared = (mesh->V.row(mesh->EV(i,0))-mesh->V.row(mesh->EV(i,1))).squaredNorm();
      stiffnessWeights(i)=3*primalLengthSquared/(massWeights(mesh->EF(i,0))+massWeights(mesh->EF(i,0)));
    }
  
  }
  
  
  //projecting an arbitrary set of extrinsic vectors (e.g. coming from user-prescribed constraints) into intrinsic vectors.
  Eigen::MatrixXd  virtual IGL_INLINE project_to_intrinsic(const Eigen::VectorXi& tangentSpaces, const Eigen::MatrixXd& extDirectionals) const{
    assert(tangentSpaces.rows()==extDirectionals.rows() || tangentSpaces.rows()==0);
    
    Eigen::VectorXi actualTangentSpaces;
    if (tangentSpaces.rows()==0)
      actualTangentSpaces = Eigen::VectorXi::LinSpaced(sources.rows(), 0, sources.rows()-1);
    else
      actualTangentSpaces = tangentSpaces;
    
    int N = extDirectionals.cols()/3;
    Eigen::MatrixXd intDirectionals(actualTangentSpaces.rows(),2*N);
    
    for (int i=0;i<actualTangentSpaces.rows();i++)
      for (int j=0;j<N;j++)
      intDirectionals.block(i,2*j,1,2)<<(extDirectionals.block(i,3*j,1,3).array()*mesh->FBx.row(actualTangentSpaces(i)).array()).sum(),(extDirectionals.block(i,3*j,1,3).array()*mesh->FBy.row(actualTangentSpaces(i)).array()).sum();
    
    return intDirectionals;
  }

  
  //projecting intrinsic to extrinsic
  Eigen::MatrixXd virtual IGL_INLINE project_to_extrinsic(const Eigen::VectorXi& tangentSpaces, const Eigen::MatrixXd& intDirectionals) const {
    
    assert(tangentSpaces.rows()==intDirectionals.rows() || tangentSpaces.rows()==0);
    Eigen::VectorXi actualTangentSpaces;
    if (tangentSpaces.rows()==0)
      actualTangentSpaces = Eigen::VectorXi::LinSpaced(sources.rows(), 0, sources.rows()-1);
    else
      actualTangentSpaces = tangentSpaces;
    
    int N = intDirectionals.cols()/2;
    Eigen::MatrixXd extDirectionals(actualTangentSpaces.rows(),3);
    
    extDirectionals.conservativeResize(intDirectionals.rows(),intDirectionals.cols()*3/2);
    for (int i=0;i<intDirectionals.rows();i++)
      for (int j=0;j<intDirectionals.cols();j+=2)
        extDirectionals.block(i,3*j/2,1,3)=mesh->FBx.row(actualTangentSpaces(i))*intDirectionals(i,j)+mesh->FBy.row(actualTangentSpaces(i))*intDirectionals(i,j+1);
    
    return extDirectionals;
  }

    void virtual IGL_INLINE interpolate(const Eigen::MatrixXi &elemIndices,
                                        const Eigen::MatrixXd &baryCoords,
                                        const Eigen::MatrixXd &intDirectionals,
                                        Eigen::MatrixXd& interpSources,
                                        Eigen::MatrixXd& interpNormals,
                                        Eigen::MatrixXd& interpField) {

      assert(elemIndices.rows()==baryCoords.rows());
      assert(baryCoords.rows()==intDirectionals.rows());

      interpSources=Eigen::MatrixXd::Zero(elemIndices.rows(),3);
      interpNormals=Eigen::MatrixXd::Zero(elemIndices.rows(),3);
      interpField=Eigen::MatrixXd::Zero(elemIndices.rows(),3);

      //in face based fields the only thing that matters is the coordinates
      for (int i=0;i<elemIndices.rows();i++){
          int j=0;
          while (elemIndices(i,j)>0){
              interpSources.row(i).array()+=mesh->V.row(elemIndices(i)).array()*baryCoords(i,j);
              interpNormals.row(i).array()+=mesh->V.row(elemIndices(i)).array()*baryCoords(i,j);
          }
          interpField.row(i)=intDirectionals(elemIndices(i),0)*mesh->FBx.row(elemIndices(i))+intDirectionals(elemIndices(i),0)*mesh->FBy.row(elemIndices(i));
      }

    }

    Eigen::SparseMatrix<double> IGL_INLINE gradient_operator(const int N,
                                                             const boundCondTypeEnum boundCondType){
      assert(hasCochainSequence()==true);
      Eigen::SparseMatrix<double> singleGradMatrix(2*mesh->F.size(), mesh->V.size());
      std::vector<Eigen::Triplet<double>> singleGradMatTriplets;
      for (int i=0;i<mesh->F.rows();i++)
          for (int j=0;j<3;j++){
              Eigen::RowVector3d e = mesh->V.row(mesh->F(i,(j+2)%3))-mesh->V.row(mesh->F(i,(j+1)%3));
              Eigen::RowVector2d eperp; eperp<<e.dot(-mesh->FBy.row(i)),e.dot(mesh->FBx.row(i));
              singleGradMatTriplets.push_back(Eigen::Triplet<double>(2*i,j,eperp(0)));
              singleGradMatTriplets.push_back(Eigen::Triplet<double>(2*i+1,j,eperp(1)));
          }

      singleGradMatrix.setFromTriplets(singleGradMatTriplets.begin(), singleGradMatTriplets.end());

      if (N==1)
          return singleGradMatrix;

      //else, kroning matrix.
      Eigen::SparseMatrix<double> gradMatrixN(2*N*mesh->F.rows(), N*mesh->V.rows());
      std::vector<Eigen::Triplet<double>> gradMatrixNTris;
      for (int i=0;i<singleGradMatTriplets.size();i++)
          for (int j=0;j<N;j++)
              gradMatrixNTris.push_back(Eigen::Triplet<double>(N*(singleGradMatTriplets[i].row()-singleGradMatTriplets[i].row()%2)+2*j+singleGradMatTriplets[i].row()%2, singleGradMatTriplets[i].col()*N+j, singleGradMatTriplets[i].value()));

      return gradMatrixN;
      //TODO: boundaries
    }

    Eigen::SparseMatrix<double> IGL_INLINE curl_operator(const int N,
                                                         const boundCondTypeEnum boundCondType,
                                                         const Eigen::VectorXi& matching){
      assert(hasCochainSequence()==true);

      Eigen::SparseMatrix<double> singleCurlMatrix(mesh->innerEdges.size(), 2*mesh->F.rows());
      std::vector<Eigen::Triplet<double>> singleCurlMatTris;
      for (int i=0;i<mesh->innerEdges.size();i++){
          Eigen::RowVector3d e = mesh->V.row(mesh->EV(mesh->innerEdges(i),1))-mesh->V.row(mesh->EV(mesh->innerEdges(i),0));
          //curl is <right_face - left_face , e>
          Eigen::RowVector2d einLeft; einLeft<<e.dot(mesh->FBx.row(mesh->EF(mesh->innerEdges(i),0))),
                                               e.dot(mesh->FBy.row(mesh->EF(mesh->innerEdges(i),0)));

          Eigen::RowVector2d einRight; einRight<<e.dot(mesh->FBx.row(mesh->EF(mesh->innerEdges(i),1))),
                  e.dot(mesh->FBy.row(mesh->EF(mesh->innerEdges(i),1)));

          singleCurlMatTris.push_back(Eigen::Triplet<double>(i, 2*mesh->EF(mesh->innerEdges(i),0),-einLeft(0)));
          singleCurlMatTris.push_back(Eigen::Triplet<double>(i, 2*mesh->EF(mesh->innerEdges(i),0)+1,-einLeft(1)));
          singleCurlMatTris.push_back(Eigen::Triplet<double>(i, 2*mesh->EF(mesh->innerEdges(i),1),einRight(0)));
          singleCurlMatTris.push_back(Eigen::Triplet<double>(i, 2*mesh->EF(mesh->innerEdges(i),1)+1,einRight(1)));
      }

      singleCurlMatrix.setFromTriplets(singleCurlMatTris.begin(), singleCurlMatTris.end());

      if (N==1)
          return singleCurlMatrix;

      Eigen::SparseMatrix<double> curlMatrixN(N*mesh->innerEdges.size(), 2*N*mesh->F.rows());
      std::vector<Eigen::Triplet<double>> curlMatNTris;

      //TODO: entire thing with matching
    }

};

}



#endif
  
