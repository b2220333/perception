#include <uima/api.hpp>

//PCL
#include <pcl/point_types.h>
#include <pcl/point_cloud.h>
#include <pcl/visualization/pcl_visualizer.h>
#include <pcl/common/pca.h>
#include <pcl/common/geometry.h>
#include <pcl/common/centroid.h>
#include <pcl/point_types_conversion.h>

//RS
#include <rs/types/all_types.h>
#include <rs/DrawingAnnotator.h>

#include <rs/scene_cas.h>
#include <rs/utils/time.h>



using namespace uima;

typedef pcl::PointXYZRGBA PointXYZRGBA;

struct featureSet{
  Eigen::Matrix3f pca_eigen_vec;
  Eigen::Vector3f pca_eigen_vals;
  float h_mean; 
  float s_mean; 
  float v_mean; 
  //float max_dist; //btw two points within the cloud  
};

class SpatulaRecognition : public DrawingAnnotator
{
private:
  double pointSize;
  pcl::PointCloud<PointXYZRGBA>::Ptr cloud_ptr;
  pcl::PointCloud<pcl::PointXYZ>::Ptr spatula;
  //std::vector<Eigen::Matrix3f> obj_orientation;
  std::vector<Eigen::Vector3f>  obj_position;
  std::vector<featureSet> obj_feats;
  std::vector<rs::Cluster> clusters;
  featureSet spatula_features;
  float range;
  float vector_length;

  float maxDist(pcl::PointCloud<pcl::PointXYZ>::Ptr);
  featureSet computeFeatures(pcl::PointCloud<pcl::PointXYZRGBA>::Ptr);
  bool hasSimilarFS(featureSet , featureSet);

public:

  SpatulaRecognition(): DrawingAnnotator(__func__), pointSize(1){

      cloud_ptr = pcl::PointCloud<pcl::PointXYZRGBA>::Ptr(new pcl::PointCloud<pcl::PointXYZRGBA>);
  }

  void fillVisualizerWithLock(pcl::visualization::PCLVisualizer &visualizer, const bool firstRun);
  TyErrorId processWithLock(CAS &tcas, ResultSpecification const &res_spec); 
  TyErrorId destroy();
  TyErrorId initialize(AnnotatorContext &ctx);
};


TyErrorId SpatulaRecognition::initialize(AnnotatorContext &ctx)
{
  if(ctx.isParameterDefined("range")) ctx.extractValue("range", range);
  if(ctx.isParameterDefined("vector_length")) ctx.extractValue("vector_length", vector_length);
  float eigen_vec;
  if(ctx.isParameterDefined("eigen_vec_0"))
  {
    ctx.extractValue("eigen_vec_0", eigen_vec);
    this->spatula_features.pca_eigen_vec(0, 0) = eigen_vec;
    outInfo("set vaule: eigen_vec_0");
  }
  if(ctx.isParameterDefined("eigen_vec_1"))
  {
    ctx.extractValue("eigen_vec_1", eigen_vec);
    this->spatula_features.pca_eigen_vec(1, 0) = eigen_vec;  
    outInfo("set vaule: eigen_vec_1");
  }
  if(ctx.isParameterDefined("eigen_vec_2"))
  {
    ctx.extractValue("eigen_vec_2", eigen_vec);
    this->spatula_features.pca_eigen_vec(1, 2) = eigen_vec;
    outInfo("set vaule: eigen_vec_2");
  }

  float eigen_val;
  if(ctx.isParameterDefined("eigen_val_0"))
  {
    ctx.extractValue("eigen_val_0", eigen_val);
    this->spatula_features.pca_eigen_vals[0] = eigen_val;
    outInfo("set vaule: eigen_val_0");
  }
  if(ctx.isParameterDefined("eigen_val_1"))
  {
    ctx.extractValue("eigen_val_1", eigen_val);
    this->spatula_features.pca_eigen_vals[1] = eigen_val;
    outInfo("set vaule: eigen_vec_1");
  }
  if(ctx.isParameterDefined("eigen_val_2"))
  {
    ctx.extractValue("eigen_val_2", eigen_val);
    this->spatula_features.pca_eigen_vals[2] = eigen_val;
    outInfo("set vaule: eigen_val_2");
  }
  float hsv;
  if(ctx.isParameterDefined("h_val"))
  {
    ctx.extractValue("h_val", hsv);
    this->spatula_features.h_mean = hsv;
    outInfo("set vaule: h_val");
  }
  if(ctx.isParameterDefined("s_val"))
  {
    ctx.extractValue("s_val", hsv);
    this->spatula_features.s_mean = hsv;
    outInfo("set vaule: s_val");
  }
  if(ctx.isParameterDefined("v_val"))
  {
    ctx.extractValue("v_val", hsv);
    this->spatula_features.v_mean = hsv;
    outInfo("set vaule: v_val");
  }
/*
  if(ctx.isParameterDefined("max_dist")) 
  {
      ctx.extractValue("max_dist", this->spatula_features.max_dist);
      outInfo("set value: max_dist");
  }
*/  
  outInfo("initialize");
  return UIMA_ERR_NONE;
}

TyErrorId SpatulaRecognition::destroy()
{
  outInfo("destroy");
  return UIMA_ERR_NONE;
}

TyErrorId SpatulaRecognition::processWithLock(CAS &tcas, ResultSpecification const &res_spec)
{
  outInfo("process start");
  this->clusters.clear();
  this->obj_position.clear();
  this->obj_feats.clear();
  //this->obj_orientation.clear();

  /*
  rs::StopWatch clock;
  outInfo("took: " << clock.getTime() << " ms.");
*/
  rs::SceneCas cas(tcas);
  rs::Scene scene = cas.getScene();
  cas.get(VIEW_CLOUD,*cloud_ptr);

  scene.identifiables.filter(clusters);

  for (rs::Cluster cluster : clusters)
  {
    pcl::PointIndices::Ptr cluster_indices(new pcl::PointIndices);
    rs::ReferenceClusterPoints clusterpoints(cluster.points());
    rs::conversion::from(clusterpoints.indices(), *cluster_indices);

    pcl::PointCloud<pcl::PointXYZRGBA>::Ptr temp(new pcl::PointCloud<pcl::PointXYZRGBA>());
    
    for(std::vector<int>::const_iterator pit = cluster_indices->indices.begin();
        pit != cluster_indices->indices.end(); pit++)
    {
      temp->points.push_back(cloud_ptr->points[*pit]);
    }
    
    pcl::PointCloud<pcl::PointXYZ>::Ptr object_cloud(new pcl::PointCloud<pcl::PointXYZ>());
    pcl::copyPointCloud(*temp, *object_cloud);

    object_cloud->width = object_cloud->points.size();
    object_cloud->height = 1;
    object_cloud->is_dense = true;
/*
    pcl::PCA<pcl::PointXYZ> spat_axis;
    spat_axis.setInputCloud(object_cloud);
    obj_orientation.push_back(spat_axis.getEigenVectors());
*/
    this->obj_feats.push_back(computeFeatures(temp));
    Eigen::Vector4f center_temp;
    pcl::compute3DCentroid(*object_cloud, center_temp);
    Eigen::Vector3f centroid;
    centroid << center_temp.x(), center_temp.y(), center_temp.z();
    obj_position.push_back(centroid);
    /*
    */
  }

  if (obj_feats.size() != obj_position.size())
  {
    outInfo("Number of available object positions does not match number of availabe object feature sets!");
    return UIMA_ERR_NONE;
  }

  outInfo("process stop");
  return UIMA_ERR_NONE;
}

/*
struct featureSet{
  Eigen::Matrix3f pca_eigen_vec;
  Eigen::Vector3f pca_eigen_vals;
  double h_mean; 
  double s_mean; 
  double v_mean; 
};
*/

void SpatulaRecognition::fillVisualizerWithLock(pcl::visualization::PCLVisualizer &visualizer, const bool firstRun)
{
  
  std::string cloudname("scene points");
  
  if (firstRun){
    outInfo("1Cloud size: " << cloud_ptr->points.size());
    visualizer.addPointCloud(cloud_ptr, cloudname);
    visualizer.setPointCloudRenderingProperties(pcl::visualization::PCL_VISUALIZER_POINT_SIZE, pointSize, cloudname);
  } else {
    outInfo("0Cloud size: " << cloud_ptr->points.size());
    visualizer.updatePointCloud(cloud_ptr, cloudname);
    visualizer.removeAllShapes();
    visualizer.getPointCloudRenderingProperties(pcl::visualization::PCL_VISUALIZER_POINT_SIZE, pointSize, cloudname);
  }

  outInfo("total obj no " + std::to_string(obj_feats.size()));
  if (obj_feats.size() == obj_position.size())
  {
    for (int i = 0; i<obj_position.size(); i++)
    {
      pcl::PointXYZ pos(obj_position[i].x(), obj_position[i].y(), obj_position[i].z());
      pcl::PointXYZ to(vector_length*(obj_position[i].x()-obj_feats[i].pca_eigen_vec(0, 0)), vector_length*(obj_position[i].y()-obj_feats[i].pca_eigen_vec(1, 0)), vector_length*(obj_position[i].z()-obj_feats[i].pca_eigen_vec(2, 0)));
      visualizer.addLine(pos, to, 1, 0, 0, std::to_string(i)+"_a");
      std::string obj_description;
      obj_description = std::to_string(obj_feats[i].pca_eigen_vec(0,0))+", "+std::to_string(obj_feats[i].pca_eigen_vec(1,0))+", "+std::to_string(obj_feats[i].pca_eigen_vec(2,0))+"\n"
                        +std::to_string(obj_feats[i].pca_eigen_vals[0])+", "+std::to_string(obj_feats[i].pca_eigen_vals[1])+", "+std::to_string(obj_feats[i].pca_eigen_vals[2])+"\n"
                        +std::to_string(obj_feats[i].h_mean)+", "+std::to_string(obj_feats[i].s_mean)+", "+std::to_string(obj_feats[i].v_mean);
      visualizer.addText3D(obj_description.c_str(), pos, 0.005);
      if(hasSimilarFS(spatula_features, this->obj_feats[i]))
      {
        outInfo("detected Spatula!!!");
        visualizer.addText3D("spatula"+std::to_string(i), pos, 0.002);
      }
    }
  }
  else
  {
    outError("the sizes of obj_feats and obj_position do not match!");
  }
}

float SpatulaRecognition::maxDist(pcl::PointCloud<pcl::PointXYZ>::Ptr cluster) {
outInfo("start maxDist");
pcl::PointXYZ tmp_begin, tmp_end, begin, end;
std::vector<pcl::PointXYZ> endpoints(2);
int size = cluster->size(); 
float currDistance = 0;    
for (int i = 0; i < size; i++) {
  begin = cluster->points[i];
  for (int j = i+1; j < size; j++) {
    end = cluster->points[j];
    if (pcl::geometry::distance(begin, end) > currDistance) {
      endpoints[0] = begin;
      endpoints[1] = end;
      currDistance = pcl::geometry::distance(begin, end);
    }
  }
}
outInfo("stop maxDist");
return currDistance;
}

featureSet SpatulaRecognition::computeFeatures(pcl::PointCloud<pcl::PointXYZRGBA>::Ptr cluster)
{
  outInfo("start computeFeatures");
  featureSet cluster_feats;
  
  pcl::PointCloud<pcl::PointXYZ>::Ptr space_cloud(new pcl::PointCloud<pcl::PointXYZ>());
  pcl::copyPointCloud(*cluster, *space_cloud);

  space_cloud->width = space_cloud->points.size();
  space_cloud->height = 1;
  space_cloud->is_dense = true;
  
  //get Eigenvectors
  pcl::PCA<pcl::PointXYZ> cluster_axis;
  cluster_axis.setInputCloud(space_cloud);
  cluster_feats.pca_eigen_vec = cluster_axis.getEigenVectors();

  
  //compute their magnitude
  cluster_feats.pca_eigen_vals = cluster_axis.getEigenValues();
  
  //compute rgb centroid
  pcl::CentroidPoint<pcl::PointXYZRGBA> centroidComputer;
  for(auto point = cluster->begin(); point != cluster->end(); point++)
    centroidComputer.add(*point);
  pcl::PointXYZRGBA centroid;
  centroidComputer.get(centroid);
  
  pcl::PointXYZHSV hsv;
  pcl::PointXYZRGBAtoXYZHSV(centroid, hsv);
  cluster_feats.h_mean = hsv.h;
  cluster_feats.s_mean = hsv.s;
  cluster_feats.v_mean = hsv.v;
  
  //get max distance within cloud
  //cluster_feats.max_dist = std::abs(maxDist(space_cloud));
  outInfo("stop computeFeatures");

  return cluster_feats;
}

bool SpatulaRecognition::hasSimilarFS(featureSet compared, featureSet comparing)
{
  outInfo("start hasSimilarFS");
  //float range = 0.1;
  //check eigenvectors
  //check eigenvalues
  Eigen::Vector3f eV_range = compared.pca_eigen_vals * 0.1;
  Eigen::Vector3f eV_min = compared.pca_eigen_vals - eV_range;
  Eigen::Vector3f eV_max = compared.pca_eigen_vals + eV_range;

  if((comparing.pca_eigen_vals[0] < eV_min[0])||(comparing.pca_eigen_vals[1] < eV_min[1])||(comparing.pca_eigen_vals[2] < eV_min[2])) return false;
  if((comparing.pca_eigen_vals[0] > eV_max[0])||(comparing.pca_eigen_vals[1] > eV_max[1])||(comparing.pca_eigen_vals[2] > eV_max[2])) return false;

  //check hsv
  if (((compared.h_mean-(range*compared.h_mean))>comparing.h_mean)||((compared.s_mean-(range*compared.s_mean))>comparing.s_mean)||((compared.v_mean-(range*compared.v_mean))>comparing.v_mean)) return false;
  if (((compared.h_mean+(range*compared.h_mean))<comparing.h_mean)||((compared.s_mean+(range*compared.s_mean))<comparing.s_mean)||((compared.v_mean+(range*compared.v_mean))<comparing.v_mean)) return false;
  
  //check maxDist
  //if ((compared.max_dist + (compared.max_dist*0.1)) > comparing.max_dist) return false;
  //if ((compared.max_dist - (compared.max_dist*0.1)) > comparing.max_dist) return false;

  outInfo("stop hasSimilarFS");
  return true;
}
// This macro exports an entry point that is used to create the annotator.
MAKE_AE(SpatulaRecognition)