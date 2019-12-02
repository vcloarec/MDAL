/*
 MDAL - Mesh Data Abstraction Library (MIT License)
 Copyright (C) 2019 Peter Petrik (zilolv at gmail dot com)
*/

#include "mdal_tuflowfv.hpp"
#include "mdal.h"
#include "mdal_utils.hpp"
#include "mdal_netcdf.hpp"
#include <math.h>
#include <assert.h>
#include <cstring>

MDAL::TuflowFVDataset3D::TuflowFVDataset3D(
  MDAL::DatasetGroup *parent,
  int ncid_x,
  int ncid_y,
  size_t timesteps,
  size_t volumesCount,
  size_t facesCount,
  size_t levelFacesCount,
  size_t ts,
  size_t maximumLevelsCount,
  std::shared_ptr<NetCDFFile> ncFile )
  : MDAL::Dataset3D( parent, volumesCount, maximumLevelsCount )
  , mNcidX( ncid_x )
  , mNcidY( ncid_y )
  , mTimesteps( timesteps )
  , mFacesCount( facesCount )
  , mLevelFacesCount( levelFacesCount )
  , mTs( ts )
  , mNcFile( ncFile )
{
  if ( ncFile )
  {
    mNcidVerticalLevels = ncFile->arrId( "NL" );
    mNcidVerticalLevelsZ = ncFile->arrId( "layerface_Z" );
    mNcidActive2D = ncFile->arrId( "stat" );
    mNcid3DTo2D = ncFile->arrId( "idx2" );
    mNcid2DTo3D = ncFile->arrId( "idx3" );
  }
}

MDAL::TuflowFVDataset3D::~TuflowFVDataset3D() = default;

size_t MDAL::TuflowFVDataset3D::verticalLevelCountData( size_t indexStart, size_t count, int *buffer )
{
  if ( ( count < 1 ) || ( indexStart >= mFacesCount ) )
    return 0;
  if ( mNcidVerticalLevels < 0 )
    return 0;

  size_t copyValues = std::min( mFacesCount - indexStart, count );
  std::vector<int> vals = mNcFile->readIntArr(
                            mNcidVerticalLevels,
                            indexStart,
                            copyValues
                          );
  memcpy( buffer, vals.data(), copyValues * sizeof( int ) );
  return copyValues;
}

size_t MDAL::TuflowFVDataset3D::verticalLevelData( size_t indexStart, size_t count, double *buffer )
{
  if ( ( count < 1 ) || ( indexStart >= mLevelFacesCount ) )
    return 0;
  if ( mTs >= mTimesteps )
    return 0;
  if ( mNcidVerticalLevelsZ < 0 )
    return 0;

  size_t copyValues = std::min( mLevelFacesCount - indexStart, count );
  std::vector<double> vals = mNcFile->readDoubleArr(
                               mNcidVerticalLevelsZ,
                               mTs,
                               indexStart,
                               1,
                               copyValues
                             );
  memcpy( buffer, vals.data(), copyValues * sizeof( double ) );
  return copyValues;
}

size_t MDAL::TuflowFVDataset3D::faceToVolumeData( size_t indexStart, size_t count, int *buffer )
{
  if ( ( count < 1 ) || ( indexStart >= mFacesCount ) )
    return 0;
  if ( mNcid2DTo3D < 0 )
    return 0;

  size_t copyValues = std::min( mFacesCount - indexStart, count );
  std::vector<int> vals = mNcFile->readIntArr(
                            mNcid2DTo3D,
                            indexStart,
                            copyValues
                          );

  // indexed from 1 in FV, from 0 in MDAL
  for ( auto &element : vals )
    element -= 1;

  memcpy( buffer, vals.data(), copyValues * sizeof( int ) );
  return copyValues;
}

size_t MDAL::TuflowFVDataset3D::scalarVolumesData( size_t indexStart, size_t count, double *buffer )
{
  if ( ( count < 1 ) || ( indexStart >= volumesCount() ) )
    return 0;
  if ( mTs >= mTimesteps )
    return 0;

  size_t copyValues = std::min( volumesCount() - indexStart, count );
  std::vector<double> vals = mNcFile->readDoubleArr(
                               mNcidX,
                               mTs,
                               indexStart,
                               1,
                               copyValues
                             );
  memcpy( buffer, vals.data(), copyValues * sizeof( double ) );
  return copyValues;
}

size_t MDAL::TuflowFVDataset3D::vectorVolumesData( size_t indexStart, size_t count, double *buffer )
{
  if ( ( count < 1 ) || ( indexStart >= volumesCount() ) )
    return 0;
  if ( mTs >= mTimesteps )
    return 0;

  size_t copyValues = std::min( volumesCount() - indexStart, count );
  std::vector<double> vals_x = mNcFile->readDoubleArr(
                                 mNcidX,
                                 mTs,
                                 indexStart,
                                 1,
                                 copyValues
                               );
  std::vector<double> vals_y = mNcFile->readDoubleArr(
                                 mNcidY,
                                 mTs,
                                 indexStart,
                                 1,
                                 copyValues
                               );

  for ( size_t i = 0; i < copyValues; ++i )
  {
    buffer[2 * i] = vals_x[i];
    buffer[2 * i + 1] = vals_y[i];
  }
  return copyValues;
}

size_t MDAL::TuflowFVDataset3D::activeVolumesData( size_t indexStart, size_t count, int *buffer )
{
  // TODO
  MDAL_UNUSED( indexStart )
  memset( buffer, 1, count * sizeof( int ) );
  return count;
}

// ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

MDAL::DriverTuflowFV::DriverTuflowFV():
  DriverCF( "TUFLOWFV",
            "TUFLOW FV",
            "*.nc",
            Capability::ReadMesh
          )
{
}

MDAL::DriverTuflowFV::~DriverTuflowFV() = default;

MDAL::DriverTuflowFV *MDAL::DriverTuflowFV::create()
{
  return new DriverTuflowFV();
}

MDAL::CFDimensions MDAL::DriverTuflowFV::populateDimensions( )
{
  CFDimensions dims;
  size_t count;
  int ncid;

  // 2D Mesh
  mNcFile->getDimension( "NumCells2D", &count, &ncid );
  dims.setDimension( CFDimensions::Face2D, count, ncid );

  mNcFile->getDimension( "MaxNumCellVert", &count, &ncid );
  dims.setDimension( CFDimensions::MaxVerticesInFace, count, ncid );

  mNcFile->getDimension( "NumVert2D", &count, &ncid );
  dims.setDimension( CFDimensions::Vertex2D, count, ncid );

  // 3D Mesh
  mNcFile->getDimension( "NumCells3D", &count, &ncid );
  dims.setDimension( CFDimensions::Volume3D, count, ncid );

  mNcFile->getDimension( "NumLayerFaces3D", &count, &ncid );
  dims.setDimension( CFDimensions::StackedFace3D, count, ncid );

  // Time
  mNcFile->getDimension( "Time", &count, &ncid );
  dims.setDimension( CFDimensions::Time, count, ncid );

  return dims;
}

void MDAL::DriverTuflowFV::populateFacesAndVertices( Vertices &vertices, Faces &faces )
{
  populateVertices( vertices );
  populateFaces( faces );
}

void MDAL::DriverTuflowFV::populateVertices( MDAL::Vertices &vertices )
{
  assert( vertices.empty() );
  size_t vertexCount = mDimensions.size( CFDimensions::Vertex2D );
  vertices.resize( vertexCount );
  Vertex *vertexPtr = vertices.data();

  // Parse 2D Mesh
  const std::vector<double> vertices2D_x = mNcFile->readDoubleArr( "node_X", vertexCount );
  const std::vector<double> vertices2D_y = mNcFile->readDoubleArr( "node_Y", vertexCount );
  const std::vector<double> vertices2D_z = mNcFile->readDoubleArr( "node_Zb", vertexCount );

  for ( size_t i = 0; i < vertexCount; ++i, ++vertexPtr )
  {
    vertexPtr->x = vertices2D_x[i];
    vertexPtr->y = vertices2D_y[i];
    vertexPtr->z = vertices2D_z[i];
  }
}

void MDAL::DriverTuflowFV::populateFaces( MDAL::Faces &faces )
{
  assert( faces.empty() );
  size_t faceCount = mDimensions.size( CFDimensions::Face2D );
  size_t vertexCount = mDimensions.size( CFDimensions::Vertex2D );
  faces.resize( faceCount );

  // Parse 2D Mesh
  size_t verticesInFace = mDimensions.size( CFDimensions::MaxVerticesInFace );
  std::vector<int> face_nodes_conn = mNcFile->readIntArr( "cell_node", faceCount * verticesInFace );
  std::vector<int> face_vertex_counts = mNcFile->readIntArr( "cell_Nvert", faceCount );

  for ( size_t i = 0; i < faceCount; ++i )
  {
    size_t nVertices = static_cast<size_t>( face_vertex_counts[i] );
    std::vector<size_t> idxs;

    for ( size_t j = 0; j < nVertices; ++j )
    {
      size_t idx = verticesInFace * i + j;
      size_t val = static_cast<size_t>( face_nodes_conn[idx] - 1 ); //indexed from 1
      assert( val < vertexCount );
      idxs.push_back( val );
    }
    faces[i] = idxs;
  }
}

void MDAL::DriverTuflowFV::calculateMaximumLevelCount()
{
  if ( mMaximumLevelsCount < 0 )
  {
    mMaximumLevelsCount = 0;
    int ncidVerticalLevels = mNcFile->arrId( "NL" );
    if ( ncidVerticalLevels < 0 )
      return;

    const size_t maxBufferLength = 1000;
    size_t indexStart = 0;
    size_t facesCount = mDimensions.size( CFDimensions::Face2D );
    while ( true )
    {
      size_t copyValues = std::min( facesCount - indexStart, maxBufferLength );
      if ( copyValues <= 0 ) break;
      std::vector<int> vals = mNcFile->readIntArr(
                                ncidVerticalLevels,
                                indexStart,
                                copyValues
                              );

      mMaximumLevelsCount = std::max( mMaximumLevelsCount, *std::max_element( vals.begin(), vals.end() ) );
      indexStart += copyValues;
    }
  }
}

void MDAL::DriverTuflowFV::addBedElevation( MDAL::MemoryMesh *mesh )
{
  MDAL::addBedElevationDatasetGroup( mesh, mesh->vertices );
}

std::string MDAL::DriverTuflowFV::getCoordinateSystemVariableName()
{
  return "";
}

std::set<std::string> MDAL::DriverTuflowFV::ignoreNetCDFVariables()
{
  std::set<std::string> ignore_variables;

  ignore_variables.insert( getTimeVariableName() );
  ignore_variables.insert( "NL" );
  ignore_variables.insert( "cell_Nvert" );
  ignore_variables.insert( "cell_node" );
  ignore_variables.insert( "idx2" );
  ignore_variables.insert( "idx3" );
  ignore_variables.insert( "cell_X" );
  ignore_variables.insert( "cell_Y" );
  ignore_variables.insert( "cell_Zb" );
  ignore_variables.insert( "cell_A" );
  ignore_variables.insert( "node_X" );
  ignore_variables.insert( "node_Y" );
  ignore_variables.insert( "node_Zb" );
  ignore_variables.insert( "layerface_Z" );
  ignore_variables.insert( "stat" );

  return ignore_variables;
}

void MDAL::DriverTuflowFV::parseNetCDFVariableMetadata( int varid, const std::string &variableName, std::string &name, bool *is_vector, bool *is_x )
{
  *is_vector = false;
  *is_x = true;

  std::string long_name = mNcFile->getAttrStr( "long_name", varid );
  if ( long_name.empty() )
  {
    name = variableName;
  }
  else
  {
    if ( MDAL::startsWith( long_name, "maximum value of " ) )
      long_name = MDAL::replace( long_name, "maximum value of ", "" ) + "/Maximums";

    if ( MDAL::startsWith( long_name, "minimum value of " ) )
      long_name = MDAL::replace( long_name, "minimum value of ", "" ) + "/Minimums";

    if ( MDAL::startsWith( long_name, "time at maximum value of " ) )
      long_name = MDAL::replace( long_name, "time at maximum value of ", "" ) + "/Time at Maximums";

    if ( MDAL::startsWith( long_name, "time at minimum value of " ) )
      long_name = MDAL::replace( long_name, "time at minimum value of ", "" ) + "/Time at Minimums";

    if ( MDAL::startsWith( long_name, "x_" ) )
    {
      *is_vector = true;
      name = MDAL::replace( long_name, "x_", "" );
    }
    else if ( MDAL::startsWith( long_name, "y_" ) )
    {
      *is_vector = true;
      *is_x = false;
      name = MDAL::replace( long_name, "y_", "" );
    }
    else
    {
      name = long_name;
    }
  }
}

std::string MDAL::DriverTuflowFV::getTimeVariableName() const
{
  return "ResTime";
}

std::shared_ptr<MDAL::Dataset> MDAL::DriverTuflowFV::create3DDataset( std::shared_ptr<MDAL::DatasetGroup> group, size_t ts,
    const MDAL::CFDatasetGroupInfo &dsi,
    double, double )
{
  calculateMaximumLevelCount();

  std::shared_ptr<MDAL::TuflowFVDataset3D> dataset = std::make_shared<MDAL::TuflowFVDataset3D>(
        group.get(),
        dsi.ncid_x,
        dsi.ncid_y,
        dsi.nTimesteps,
        mDimensions.size( CFDimensions::Type::Volume3D ),
        mDimensions.size( CFDimensions::Type::Face2D ),
        mDimensions.size( CFDimensions::Type::StackedFace3D ),
        ts,
        mMaximumLevelsCount,
        mNcFile
      );

  // TODO use "Maximums" from file
  dataset->setStatistics( MDAL::calculateStatistics( dataset ) );
  return std::move( dataset );
}
