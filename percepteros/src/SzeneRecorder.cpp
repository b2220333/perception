#include <uima/api.hpp>

#include <pcl/point_types.h>
#include <rs/types/all_types.h>
//RS
#include <rs/scene_cas.h>
#include <rs/utils/time.h>
#include <std_srvs/Empty.h>
#include <suturo_perception_msgs/PerceiveAction.h>
#include <ros/time.h>

#include <pcl/filters/voxel_grid.h>
#include <pcl/filters/filter.h>

#include <pcl/features/normal_3d.h>

#include <pcl/registration/icp.h>
#include <pcl/registration/icp_nl.h>
#include <pcl/registration/transforms.h>
#include <pcl/point_cloud.h>
#include <pcl/point_representation.h>
#include <pcl/visualization/pcl_visualizer.h>
#include <rs/DrawingAnnotator.h>
#include <rs/utils/common.h>

#include <pcl/point_cloud.h>
#include <pcl/octree/octree.h>

#include <pcl/io/pcd_io.h>

#include <iostream>
#include <vector>
#include <ctime>

using namespace uima;


class SzeneRecorder : public DrawingAnnotator
{
private:

    typedef pcl::PointXYZRGBA PointT;
    typedef pcl::PointCloud<PointT> PointCloud;
    typedef pcl::PointNormal PointNormalT;
    typedef pcl::PointCloud<PointNormalT> PointCloudWithNormals;
    double pointSize = 1;
    int runCount = 0;


    pcl::PointIndices clusterIndices;

    struct SavedSzene{

        cv::Mat beforeDepthImage;
        PointCloud::Ptr beforeActionPC;
        ros::Time beforeTime;

        cv::Mat afterDepthImage;
        PointCloud::Ptr afterActionPC;
        ros::Time afterTime;

        cv::Mat dist;
    };

    ros::NodeHandle nh_;

    ros::ServiceServer beforeActionService, actionEndedService;

    bool queueSaveImage = false;

    bool firstRun = true;

    SavedSzene savedSzene;

public:

  SzeneRecorder() :DrawingAnnotator(__func__){}

  TyErrorId initialize(AnnotatorContext &ctx)
  {
    outInfo("initialize");

    nh_ = ros::NodeHandle("~");

    beforeActionService = nh_.advertiseService("before_action", &SzeneRecorder::beforeActionCallback, this);
    actionEndedService = nh_.advertiseService("perceive_action_effect", &SzeneRecorder::perceiveActionEffectCallback, this);

    savedSzene.beforeActionPC = pcl::PointCloud<pcl::PointXYZRGBA>::Ptr(new pcl::PointCloud<pcl::PointXYZRGBA>);
    savedSzene.afterActionPC = pcl::PointCloud<pcl::PointXYZRGBA>::Ptr(new pcl::PointCloud<pcl::PointXYZRGBA>);
    pcl::PointCloud<pcl::PointXYZRGBA>::Ptr cloud_ptr(new pcl::PointCloud<pcl::PointXYZRGBA>);
    return UIMA_ERR_NONE;
  }

  TyErrorId destroy()
  {
    outInfo("destroy");
    return UIMA_ERR_NONE;
  }

  TyErrorId processWithLock(CAS &tcas, ResultSpecification const &res_spec)
  {
    outInfo("process start");
    rs::StopWatch clock;
    rs::SceneCas cas(tcas);
    rs::Scene scene = cas.getScene();

    cv::Mat depth_image;

    cas.get(VIEW_DEPTH_IMAGE, depth_image);

    pcl::PointCloud<pcl::PointXYZRGBA>::Ptr cloud_ptr(new pcl::PointCloud<pcl::PointXYZRGBA>);
    cas.get(VIEW_CLOUD,*cloud_ptr);

    /*outInfo("Cloud size: " << cloud_ptr->points.size());
    outInfo("took: " << clock.getTime() << " ms.");
    if(queueSaveImage){
        queueSaveImage = false;
        if(firstRun){
            firstRun = false;
            savedSzene.beforeActionPC = cloud_ptr;
            savedSzene.beforeDepthImage = depth_image;
            pcl::io::savePCDFileASCII ("testbefore.pcd", *cloud_ptr);
        }
        else{
            savedSzene.afterActionPC = cloud_ptr;
            savedSzene.afterDepthImage = depth_image;
            pcl::io::savePCDFileASCII ("testafter.pcd", *cloud_ptr);



        }

    }*/

    if(runCount == 0){
        savedSzene.beforeActionPC = cloud_ptr;
        savedSzene.beforeDepthImage = depth_image;
        runCount ++;
    } else if(runCount == 1){
        savedSzene.afterActionPC = cloud_ptr;
        savedSzene.afterDepthImage = depth_image;
        runCount = 0;

        //savedSzene.dist = cv::substract(savedSzene.afterDepthImage,savedSzene.beforeDepthImage, savedSzene.dist);

        cv::absdiff(savedSzene.beforeDepthImage, savedSzene.afterDepthImage, savedSzene.dist);
        //cv::imshow("dist",savedSzene.beforeDepthImage);
        //dst = src1 - src2;
        //dst -= src1;

        cv::Mat I = savedSzene.dist;
        ushort min = 65535, max = 0;
        printf("Depth: %d", I.depth());
        //CV_Assert(I.depth() == CV_8U);

        int channels = I.channels();

        int nRows = I.rows;
        int nCols = I.cols * channels;


        if (I.isContinuous())
        {
            nCols *= nRows;
            nRows = 1;
        }

        int i,j;
        ushort* p;
        for( i = 0; i < nRows; ++i)
        {
            p = I.ptr<ushort>(i);
            for ( j = 0; j < nCols; ++j)
            {
                if(min > p[j]){
                  min = p[j];
                }
                if(max < p[j]){
                    max = p[j];
                }
                if(p[j]> 20){
                    p[j] = 65535;
                }
            }
        }
        printf("Min: %u, Max: %u ",min,max);

        pcl::PointIndices indices = detectChange();
        if(indices.indices.size() == 0){
            return UIMA_ERR_NONE;
        }
        outInfo("change detected");
        clusterIndices = indices;

        rs::Cluster uimaCluster = rs::create<rs::Cluster>(tcas);
        rs::ReferenceClusterPoints rcp = rs::create<rs::ReferenceClusterPoints>(tcas);
        rs::PointIndices uimaIndices = rs::conversion::to(tcas, indices);
        outInfo("Before set");
        rcp.indices.set(uimaIndices);
        outInfo("After set");

        uimaCluster.points.set(rcp);
        uimaCluster.source.set("ChangeDetection");

        scene.identifiables.append(uimaCluster);

    }

    return UIMA_ERR_NONE;
  }

  pcl::PointIndices detectChange(){
      srand ((unsigned int) time (NULL));

      // Octree resolution - side length of octree voxels
      float resolution = 0.03f;

      // Instantiate octree-based point cloud change detection class
      pcl::octree::OctreePointCloudChangeDetector<pcl::PointXYZRGBA> octree (resolution);

      // Add points from cloudA to octree
      octree.setInputCloud (savedSzene.beforeActionPC);
      octree.addPointsFromInputCloud ();

      // Switch octree buffers: This resets octree but keeps previous tree structure in memory.
      octree.switchBuffers ();

      // Add points from cloudB to octree
      octree.setInputCloud (savedSzene.afterActionPC);
      octree.addPointsFromInputCloud ();

      std::vector<int> newPointIdxVector;

      // Get vector of point indices from octree voxels which did not exist in previous buffer
      octree.getPointIndicesFromNewVoxels (newPointIdxVector);

      pcl::PointIndices indices;
      indices.indices = newPointIdxVector;
      printf("amount of indices: %d", indices.indices.size() );

      // Output points
      std::cout << "Output from getPointIndicesFromNewVoxels:" << std::endl;
      /*for (size_t i = 0; i < newPointIdxVector.size (); ++i)
        std::cout << i << "# Index:" << newPointIdxVector[i]
                  << "  Point:" << savedSzene.afterActionPC->points[newPointIdxVector[i]].x << " "
                  << savedSzene.afterActionPC->points[newPointIdxVector[i]].y << " "
                  << savedSzene.afterActionPC->points[newPointIdxVector[i]].z << std::endl;*/
      return indices;
  }



  bool beforeActionCallback(std_srvs::Empty::Request& request,
                                     std_srvs::Empty::Response& res){
      queueSaveImage = true;
      savedSzene.beforeTime = ros::Time::now();
      return true;
  }

  bool perceiveActionEffectCallback(suturo_perception_msgs::PerceiveAction::Request &req,
                                    suturo_perception_msgs::PerceiveAction::Response &res){
      queueSaveImage = true;
      savedSzene.afterTime = ros::Time::now();
      return true;
  }

  void drawImageWithLock(cv::Mat &disp)
  {
    disp = savedSzene.dist;
  }

  void fillVisualizerWithLock(pcl::visualization::PCLVisualizer &visualizer, const bool firstRun)
  {
  const std::string &cloudname = this->name;
  const pcl::PointIndices &indices = clusterIndices;
/*  if(runCount != 1 ){
      return;
  }*/
  for(size_t j = 0; j < indices.indices.size(); ++j)
  {
    size_t index = indices.indices[j];
    savedSzene.afterActionPC->points[index].rgba = rs::common::colors[0];
  }

    if(savedSzene.afterActionPC && savedSzene.afterActionPC->points.size()!=0){
        if(firstRun)
        {
          visualizer.addPointCloud(savedSzene.afterActionPC, cloudname);
          visualizer.setPointCloudRenderingProperties(pcl::visualization::PCL_VISUALIZER_POINT_SIZE, pointSize, cloudname);

        }
        else
        {
          visualizer.updatePointCloud(savedSzene.afterActionPC, cloudname);
          visualizer.getPointCloudRenderingProperties(pcl::visualization::PCL_VISUALIZER_POINT_SIZE, pointSize, cloudname);
        }
    }
  }

};

// This macro exports an entry point that is used to create the annotator.
MAKE_AE(SzeneRecorder)
