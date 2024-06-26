// This file is part of Directional, a library for directional field processing.
// Copyright (C) 2022 Amir Vaxman <avaxman@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.

#ifndef DIRECTIONAL_FACEFIELD_H
#define DIRECTIONAL_FACEFIELD_H

#include <iostream>
#include <Eigen/Geometry>
#include <Eigen/Sparse>
#include <igl/boundary_loop.h>
#include <igl/doublearea.h>
#include <directional/dual_cycles.h>
#include <directional/TriMesh.h>
#include <directional/CartesianField.h>

namespace directional{


//This class represents face-based fields, where the vectors are tangent to every face's natural tangent space, and where the dual cycles are around vertices. 
class FaceField: public CartesianField{
public:
  
  virtual discTangTypeEnum discTangType() const {return discTangTypeEnum::FACE_SPACES;}
    
  FaceField(){}
  FaceField(const TriMesh& _mesh):CartesianField(_mesh){}
  ~FaceField(){}
  
  void IGL_INLINE init_field(const TriMesh& _mesh, const int _fieldType, const int _N){
    
    typedef std::complex<double> Complex;
    mesh = &_mesh;
    fieldType = _fieldType;
    N = _N;
    
    //adjacency relation is by dual edges.
    adjSpaces = mesh->EF;
    oneRing = mesh->FE;
    sources = mesh->barycenters;
    normals = mesh->faceNormals;
    dualSources = mesh->V;
    dualNormals = mesh->vertexNormals;
    singElements = Eigen::VectorXi(0);
    singIndices = Eigen::VectorXi(0);
    intField.conservativeResize(mesh->F.rows(),2*N);
    extField.conservativeResize(mesh->F.rows(),3*N);
    
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
    directional::dual_cycles(mesh->V, mesh->F, mesh->EV, mesh->EF, dualCycles, cycleCurvatures, element2Cycle, innerAdjacencies);
    
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
  
  void IGL_INLINE set_extrinsic_field(const Eigen::MatrixXd& _extField){
  
    assert(_extField.cols()==3*N);
    
    extField=_extField;
    intField.resize(extField.rows(),2*N);
    for (int i=0;i<N;i++)
      intField.block(0,2*i,intField.rows(),2)<<(extField.block(0,3*i,extField.rows(),3).array()*mesh->FBx.array()).rowwise().sum(),(extField.block(0,3*i,extField.rows(),3).array()*mesh->FBy.array()).rowwise().sum();
   
  }
  
  void IGL_INLINE set_intrinsic_field(const Eigen::MatrixXd& _intField){
    assert (!(fieldType==POWER_FIELD) || (_intField.cols()==2));
    assert ((_intField.cols()==2*N) || !(fieldType==POLYVECTOR_FIELD || fieldType==RAW_FIELD));
    intField = _intField;
    
    //computing extrinsic field
    extField.conservativeResize(intField.rows(),intField.cols()*3/2);
    for (int i=0;i<intField.rows();i++)
      for (int j=0;j<intField.cols();j+=2)
        extField.block(i,3*j/2,1,3)=mesh->FBx.row(i)*intField(i,j)+mesh->FBy.row(i)*intField(i,j+1);
  }
  
  void IGL_INLINE set_intrinsic_field(const Eigen::MatrixXcd& _intField){
    assert (!(fieldType==POWER_FIELD) || (_intField.cols()==1));
    assert ((_intField.cols()==N) || !(fieldType==POLYVECTOR_FIELD || fieldType==RAW_FIELD));
    
    intField.resize(_intField.rows(),_intField.cols()*2);
    for (int i=0;i<_intField.cols();i++){
      intField.col(2*i)=_intField.col(i).real();
      intField.col(2*i+1)=_intField.col(i).imag();
    }
    //computing extrinsic field
    extField.conservativeResize(_intField.rows(),_intField.cols()*3);
    for (int i=0;i<_intField.rows();i++)
      for (int j=0;j<_intField.cols();j++)
        extField.block(i,3*j,1,3)=mesh->FBx.row(i)*_intField(i,j).real()+mesh->FBy.row(i)*_intField(i,j).imag();
  }
  
  Eigen::MatrixXd  virtual IGL_INLINE project_to_intrinsic(const Eigen::VectorXi& tangentSpaces, const Eigen::MatrixXd& extDirectionals) const{
    assert(tangentSpaces.rows()==extDirectionals.rows());
    Eigen::MatrixXd intDirectionals(tangentSpaces.rows(),2);
    
    //std::cout<<"tangentSpaces: "<<tangentSpaces<<std::endl;
    for (int j=0;j<tangentSpaces.rows();j++){
      /*std::cout<<"j: "<<j<<std::endl;
      std::cout<<"(extDirectionals.row(j).array()*mesh->Bx.row(tangentSpaces(j)).array()).sum(),(extDirectionals.row(j).array()*mesh->By.row(tangentSpaces(j)).array()).sum():"<<(extDirectionals.row(j).array()*mesh->Bx.row(tangentSpaces(j)).array()).sum()<<","<<(extDirectionals.row(j).array()*mesh->By.row(tangentSpaces(j)).array()).sum()<<std::endl;*/
      intDirectionals.row(j)<<(extDirectionals.row(j).array()*mesh->FBx.row(tangentSpaces(j)).array()).sum(),(extDirectionals.row(j).array()*mesh->FBy.row(tangentSpaces(j)).array()).sum();
    }
    return intDirectionals;
  }
    
  void IGL_INLINE set_singularities(const Eigen::VectorXi& _singVertices,
                                    const Eigen::VectorXi& _singIndices){
    
    Eigen::VectorXi vertexIndices=Eigen::VectorXi::Zero(mesh->V.rows());
    int singCounter=_singVertices.size();
    for (int i=0;i<_singVertices.size();i++)
      vertexIndices(_singVertices(i))=_singIndices(i);
    
    //removing boundary indices
    std::vector<std::vector<int> > L;
    igl::boundary_loop(mesh->F, L);
    for (int j=0;j<L.size();j++)
      for (int k=0;k<L[j].size();k++){
        if (vertexIndices(L[j][k])) singCounter--;
        vertexIndices(L[j][k])=0;
      }
    
    singElements.resize(singCounter);
    singIndices.resize(singCounter);
    singCounter=0;
    for (int i=0;i<mesh->V.rows();i++){
      if (vertexIndices(i)){
        singElements(singCounter)=i;
        singIndices(singCounter++)=vertexIndices(i);
      }
    }
  }
  

};

}



#endif
