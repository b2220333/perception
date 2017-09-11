#include <uima/api.hpp>

#include <pcl/point_types.h>
#include <pcl/point_cloud.h>
#include <pcl/visualization/pcl_visualizer.h>

#include <pcl/filters/passthrough.h>

//RS
#include <rs/types/all_types.h>
#include <rs/DrawingAnnotator.h>

#include <rs/scene_cas.h>
#include <rs/utils/time.h>

//CATERROS
#include <percepteros/types/all_types.h>

// for the parser
#include <ros/ros.h>
#include <ros/package.h>
#include <yaml-cpp/yaml.h>
#include <assert.h>

using namespace uima;

struct regionDescriptor{
  //filled by parser
  std::string regionID;
  //std::vector<std::string> processViews;
  Eigen::Vector3d center_position;
  Eigen::Vector3d axis_ranges;

  //supported values computed later on
  pcl::PointCloud<pcl::PointXYZRGBA>::Ptr view_cloud_ptr;
  pcl::PointCloud<pcl::Normal>::Ptr normal_ptr;
};

class ObjectRegionFilter : public DrawingAnnotator
{
private:
  std::string region_config;
  double pointSize;
  std::vector<pcl::PointCloud<pcl::PointXYZRGBA>::Ptr> cloud_ptrs;
  std::vector<regionDescriptor> regions;

  bool parseRegionConfig(std::string region_file);

public:

  ObjectRegionFilter(): 
    DrawingAnnotator(__func__),
    pointSize(1)
  {
  }

  TyErrorId initialize(AnnotatorContext &ctx);
  TyErrorId destroy();
  TyErrorId processWithLock(CAS &tcas, ResultSpecification const &res_spec); 
  void fillVisualizerWithLock(pcl::visualization::PCLVisualizer &visualizer, const bool firstRun);

};
bool ObjectRegionFilter::parseRegionConfig(std::string region_file)
{
  std::string regionConfigFile = ros::package::getPath("percepteros") +"/config/" + region_file;
  YAML::Node regionList = YAML::LoadFile(regionConfigFile);
  if(!regionList.IsSequence())
  {
    outError("RegionDefinition: YAML contains no seqquence!");
    return false;
  }
  
  if(regionList.IsNull()){ 
    outError("Config file not found!");
    return false;
  } else {
    outInfo("Config file found at: " + regionConfigFile);
  }

  for(YAML::const_iterator reg_it = regionList.begin(); reg_it != regionList.end(); ++reg_it){
    regionDescriptor rD;
    const YAML::Node& regionItem = *reg_it;
    rD.regionID = regionItem["regionID"].as<std::string>();
    outInfo("processing region with ID" + rD.regionID);

//    YAML::Node viewsToProcess = regionItem["viewsToProcess"];
//    assert(viewsToProcess.IsSequence());
//    if(viewsToProcess.size()==0)
//    {
//      outError("Parser does not accept region with 0 views!");
//      return false;
//    }
//    for(YAML::const_iterator view_it = viewsToProcess.begin(); view_it != viewsToProcess.end(); ++view_it){
//      std::string viewName = view_it->as<std::string>();
//      rD.processViews.push_back(viewName);
//    }

    YAML::Node region_center = regionItem["region_center"];
    assert(region_center.IsSequence());
    if (region_center.size() != 3)
    {
      outError("ParserError: region_center needs exactly 3 elements");
      return false;
    }
    rD.center_position = Eigen::Vector3d(region_center[0].as<double>(), region_center[1].as<double>(), region_center[2].as<double>());
    
    YAML::Node range = regionItem["range"];
    assert(range.IsSequence());
    if (range.size() != 3)
    {
      outError("ParserError: range needs exactly 3 elements");
      return false;
    }
    rD.axis_ranges = Eigen::Vector3d(range[0].as<double>(), range[1].as<double>(), range[2].as<double>());

    this->regions.push_back(rD);
  }

  return true; 
}

TyErrorId ObjectRegionFilter::processWithLock(CAS &tcas, ResultSpecification const &res_spec)
{
  outInfo("process start");
  cloud_ptrs.clear();

  rs::StopWatch clock;
  rs::SceneCas cas(tcas);
  //cas.get(VIEW_CLOUD,*cloud_ptr);

  return UIMA_ERR_NONE;
}

void ObjectRegionFilter::fillVisualizerWithLock(pcl::visualization::PCLVisualizer &visualizer, const bool firstRun)
{
  std::string cloudname("object region scene points");
  
  if (firstRun){
    visualizer.addPointCloud(cloud_ptrs[0], cloudname);
    visualizer.setPointCloudRenderingProperties(pcl::visualization::PCL_VISUALIZER_POINT_SIZE, pointSize, cloudname);
  } else {
    visualizer.updatePointCloud(cloud_ptrs[0], cloudname);
    visualizer.removeAllShapes();
    visualizer.getPointCloudRenderingProperties(pcl::visualization::PCL_VISUALIZER_POINT_SIZE, pointSize, cloudname);
  }
  visualizer.addSphere(pcl::PointXYZ(-0.387466371059, 0.170032024384, 0.9), 0.1, "spatpos");
}

TyErrorId ObjectRegionFilter::initialize(AnnotatorContext &ctx)
{
  outInfo("initialize");
  if(ctx.isParameterDefined("region_config")) ctx.extractValue("region_config", region_config);
  parseRegionConfig(region_config);
  
  return UIMA_ERR_NONE;
}

TyErrorId ObjectRegionFilter::destroy()
{
  outInfo("destroy");
  return UIMA_ERR_NONE;
}

// This macro exports an entry point that is used to create the annotator.
MAKE_AE(ObjectRegionFilter)