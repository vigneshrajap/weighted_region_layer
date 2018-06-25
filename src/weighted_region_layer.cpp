
/*********************************************************************
 *
 * Software License Agreement
 *
 *  Copyright (c) 2018, Steven Macenski
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of Steven Macenski, Inc. nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 * Author: Steve Macenski (stevenmacenski@gmail.com)
 *********************************************************************/

#include <weighted_region_layer/weighted_region_layer.hpp>
#include "src/serialization.cpp"

namespace weighted_region_layer
{

/*****************************************************************************/
WeightedRegionLayer::WeightedRegionLayer()
/*****************************************************************************/
{
  ros::NodeHandle nh("~" + name_);
  _save = nh.advertiseService("save_file", \
                                  &WeightedRegionLayer::SaveFileService, this);
  _load = nh.advertiseService("load_file", \
                                  &WeightedRegionLayer::LoadFileService, this);
}

/*****************************************************************************/
WeightedRegionLayer::~WeightedRegionLayer()
/*****************************************************************************/
{
}

/*****************************************************************************/
void WeightedRegionLayer::onInitialize()
/*****************************************************************************/
{
  current_ = true;
  _got_map = false;
  enabled_ = true;
  costmap_ = NULL;
  matchSize();
  _global_frame = layered_costmap_->getGlobalFrameID();

  ROS_INFO("Initializing the WeightedRegionLayer as %s", name_.c_str());
  ros::NodeHandle _nh("~/" + name_);

  _nh.param("map_topic", _map_topic, std::string("/map"));
  _nh.param("enable_param_updates", _enable_param_updates, false);
  _nh.param("wrl_parameter_name",_wrl_parameter_name,std::string("wrl_file"));

  if (_enable_param_updates)
  {
    _map_sub = _nh.subscribe(_map_topic, 1, \
                                     &WeightedRegionLayer::MapCallback, this);
    _nh.param(_wrl_parameter_name, _wrl_file_name, std::string("none"));    

    ROS_INFO("WeightedRegionLayer: Enabling Parameter based updates with "
             "parameter %s. Current parameter %s is %s.", 
             _wrl_parameter_name.c_str(), _wrl_parameter_name.c_str(), 
             _wrl_file_name.c_str());
  }
  else
  {
    ROS_WARN("WeightedRegionLayer: Param updates were not enabled."
             " You may update the weighted region file using "
             "the load file service.");
    return;
  }

  if (_wrl_file_name != std::string("none"))
  {
    if (IsFileValid(_wrl_file_name))
    {
      ReadFromFile(_wrl_file_name.c_str());
    }
    else
    {
      ROS_WARN("WeightedRegionLayer: %s is an invalid file name or path, "
               "have you made a wrl file for this map yet? :).", \
               _wrl_file_name.c_str());
    }
    return;
  }
    
  ROS_WARN("WeightedRegionLayer: No wrl file or parameters were specified. "
           "Will not do anything until load file service is called.");
  return;
}

/*****************************************************************************/
void WeightedRegionLayer::ChangeWeightedRegionsFile()
/*****************************************************************************/
{
  if (_nh.getParam(_wrl_parameter_name, _wrl_file_name))
  {
    if (IsFileValid(_wrl_file_name.c_str()))
    {
      ReadFromFile(_wrl_file_name.c_str());
      return;
    }
    else
    {
      ROS_WARN("WeightedRegionLayer: Failed to open file %s, does it exist?", \
                                                       _wrl_file_name.c_str());
    }
  }
  else
  {
    ROS_WARN("WeightedRegionLayer: Failed to get param %s, does it exist?", \
                                                  _wrl_parameter_name.c_str());
  }
  return;
}

/*****************************************************************************/
void WeightedRegionLayer::MapCallback( \
                                    const nav_msgs::OccupancyGridConstPtr& msg)
/*****************************************************************************/
{
  costmap_ = NULL; // new map, current information is invalid

  if (!_got_map)
  {
    _got_map = true;
  }

  _map_frame = msg->header.frame_id;
  _width = msg->info.width;
  _height = msg->info.height;
  ChangeWeightedRegionsFile();

  // TODO find length of costmap_
  // if (costmap_ != NULL && costmap_.size() != _width * _height)
  // {
  //   ROS_WARN("WeightedRegionLayer: Loaded weighted region "
  //            "map size does not match current map, invalid. Use the load"
  //            " service to change maps or this layer will not do anything.");
  //   costmap_ = NULL;
  // } 

  return;
}

/*****************************************************************************/
void WeightedRegionLayer::matchSize()
/*****************************************************************************/
{
  if (!layered_costmap_->isRolling())
  {
    Costmap2D* master = layered_costmap_->getCostmap();
    resizeMap(master->getSizeInCellsX(), master->getSizeInCellsY(), 
              master->getResolution(),
              master->getOriginX(), master->getOriginY());
  }
  return;
}

/*****************************************************************************/
void WeightedRegionLayer::updateBounds(double robot_x, double robot_y, \
                                       double robot_yaw, double* min_x,
                                       double* min_y, double* max_x, \
                                       double* max_y)
/*****************************************************************************/
{
  if (!enabled_ || !_got_map || !costmap_)
  {
    return;
  }

  useExtraBounds(min_x, min_y, max_x, max_y);
}

/*****************************************************************************/
void WeightedRegionLayer::updateCosts(costmap_2d::Costmap2D& master_grid, \
                                    int min_i, int min_j, int max_i, int max_j)
/*****************************************************************************/
{
  if (!enabled_ || !_got_map || !costmap_)
  {
    return;
  }

  if (!layered_costmap_->isRolling())
  {
    updateWithMax(master_grid, min_i, min_j, max_i, max_j);
  }
  else
  {
    ROS_ERROR_ONCE("WeightedRegionLayer: Only static costmaps does the weighed"
                  "Region Layer make sense for.");
  }
}

/*****************************************************************************/
void WeightedRegionLayer::ReadFromFile(const std::string& filename)
/*****************************************************************************/
{
  try
  {
    std::string name(filename + ".wrl");
    serialization::Read(name);
    ROS_INFO("WeightedRegionLayer: Deserialized file correctly!");
    return;
  }
  catch (...)
  {
    costmap_ = NULL;
    return;
  }
}

/*****************************************************************************/
void WeightedRegionLayer::WriteToFile(const std::string& filename)
/*****************************************************************************/
{
  std::string name(filename + ".wrl");
  serialization::Write(name);
  return;
}

/*****************************************************************************/
bool WeightedRegionLayer::LoadFileService( \
                 weighted_region_layer::LoadWeightedRegionFile::Request& req, 
                 weighted_region_layer::LoadWeightedRegionFile::Response& resp)
/*****************************************************************************/
{
  _nh.param(_wrl_parameter_name, req.filename);
  ChangeWeightedRegionsFile();
  resp.status = true;
  return true;
}

/*****************************************************************************/
bool WeightedRegionLayer::SaveFileService( \
                 weighted_region_layer::SaveWeightedRegionFile::Request& req, 
                 weighted_region_layer::SaveWeightedRegionFile::Response& resp)
/*****************************************************************************/
{
  if (IsFileValid(req.filename))
  {
    if (req.overwrite)
    {
      WriteToFile(req.filename);
    }
    else
    {
      ROS_WARN("WeightedRegionLayer: filename %s exists and you didn't"
               " enable overwrite!", req.filename.c_str());
      return true;
    }
  }
  else
  {
    WriteToFile(req.filename); 
  }
  return true;
}

} // end namespace

#include <pluginlib/class_list_macros.h>
PLUGINLIB_EXPORT_CLASS(weighted_region_layer::WeightedRegionLayer, costmap_2d::Layer)
