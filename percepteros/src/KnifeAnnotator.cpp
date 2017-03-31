#include <uima/api.hpp>

#include <pcl/point_types.h>
#include <rs/types/all_types.h>
//RS
#include <rs/scene_cas.h>
#include <rs/DrawingAnnotator.h>
#include <rs/utils/common.h>
#include <rs/utils/time.h>

#include <percepteros/types/all_types.h>

#include <geometry_msgs/PoseStamped.h>
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl/point_types_conversion.h>
#include <pcl/common/geometry.h>
#include <iostream>
#include <pcl/filters/voxel_grid.h>

#include <cmath>

//surface matching
#include <pcl/console/print.h>

using namespace uima;

typedef pcl::PointXYZRGBA PointR;
typedef pcl::PointCloud<PointR> PCR;
typedef pcl::Normal	Normal;
typedef pcl::PointCloud<Normal> PCN;
typedef pcl::PointNormal PointN;
typedef pcl::PointCloud<PointN> PC;

class KnifeAnnotator : public DrawingAnnotator
{
private:
	PCR::Ptr cloud_r = PCR::Ptr(new PCR);
	PCN::Ptr cloud_n = PCN::Ptr(new PCN);
	PC::Ptr cloud = PC::Ptr(new PC);
	PC::Ptr blade = PC::Ptr(new PC);
	int HUE_UPPER_BOUND, HUE_LOWER_BOUND;

public:
	tf::Vector3 x, y, z;
	PointN highest, lowest;

  KnifeAnnotator(): DrawingAnnotator(__func__){
  }

  TyErrorId initialize(AnnotatorContext &ctx)
  {
    outInfo("initialize");
		
		//extract color parameters
		ctx.extractValue("minHue", HUE_LOWER_BOUND);
		ctx.extractValue("maxHue", HUE_UPPER_BOUND);

    return UIMA_ERR_NONE;
  }

  TyErrorId destroy()
  {
    outInfo("destroy");
    return UIMA_ERR_NONE;
  }

  TyErrorId processWithLock(CAS &tcas, ResultSpecification const &res_spec)
  {
    outInfo("process start\n");
		//get clusters
    rs::SceneCas cas(tcas);
		rs::Scene scene = cas.getScene();
		std::vector<rs::Cluster> clusters;
		scene.identifiables.filter(clusters);

		//get scene points
		cas.get(VIEW_CLOUD, *cloud_r);
		cas.get(VIEW_NORMALS, *cloud_n);
		pcl::PointCloud<pcl::PointXYZ>::Ptr temp(new pcl::PointCloud<pcl::PointXYZ>);
		pcl::copyPointCloud(*cloud_r, *temp);
		pcl::concatenateFields(*temp, *cloud_n, *cloud);

		//helpers
		rs::StopWatch clock;
		bool found = false;
		
		std::vector<percepteros::RecognitionObject> objects;
		for (auto cluster : clusters) {
			if (cluster.source.get().compare("HueClustering") == 0) {
				objects.clear();
				cluster.annotations.filter(objects);
				
				outInfo("Average hue: " << objects[0].color.get());
				pcl::PointIndices::Ptr cluster_indices(new pcl::PointIndices);
				rs::ReferenceClusterPoints clusterpoints(cluster.points());
				rs::conversion::from(clusterpoints.indices(), *cluster_indices);
				
				outInfo("Point amount: " << cluster_indices->indices.size());

				if (objects.size() > 0 && objects[0].color.get() > HUE_LOWER_BOUND && objects[0].color.get() < HUE_UPPER_BOUND) {
					outInfo("Found knife cluster.");
					found = true;
					extractPoints(cloud, cluster);

					rs::PoseAnnotation poseA = rs::create<rs::PoseAnnotation>(tcas);
					tf::StampedTransform camToWorld;
					camToWorld.setIdentity();
					if (scene.viewPoint.has()) {
						rs::conversion::from(scene.viewPoint.get(), camToWorld);
					}
					
					x = getX(blade);
					y = getY(blade);
	
					tf::Transform transform;
					transform.setOrigin(getOrigin(blade));

					z = x.cross(y);
					y = z.cross(x);
		
					x.normalize(); y.normalize(); z.normalize();

					tf::Matrix3x3 rot;
					rot.setValue(	x[0], x[1], x[2],
												y[0], y[1], y[2],
												z[0], z[1], z[2]);
					transform.setBasis(rot);

					objects[0].name.set("Knife");
					objects[0].type.set(6);
					objects[0].width.set(0.28f);
					objects[0].height.set(0.056f);
					objects[0].depth.set(0.03f);
		
					tf::Stamped<tf::Pose> camera(transform, camToWorld.stamp_, camToWorld.child_frame_id_);
					tf::Stamped<tf::Pose> world(camToWorld * transform, camToWorld.stamp_, camToWorld.frame_id_);

					poseA.source.set("KnifeAnnotator");
					poseA.camera.set(rs::conversion::to(tcas, camera));
					poseA.world.set(rs::conversion::to(tcas, world));
			
					cluster.annotations.append(poseA);
					outInfo("Finished");
					break;
				}
			} 	
		}

		if (!found) {
			outInfo("No knife found.");
			return UIMA_ERR_NONE;
		}
		
    return UIMA_ERR_NONE;
  }

	void setEndpoints(PC::Ptr blade) {
		PointN begin, end;
		std::vector<PointN> endpoints(2);
		int size = blade->size();	
		float currDistance = 0;
		
		for (int i = 0; i < size; i++) {
			begin = blade->points[i];
			for (int j = i+1; j < size; j++) {
				end = blade->points[j];
				if (pcl::geometry::distance(begin, end) > currDistance) {
					endpoints[0] = begin;
					endpoints[1] = end;
					currDistance = pcl::geometry::distance(begin, end);
				}
			}
		}

		if (endpoints[0].x + endpoints[0].y + endpoints[0].z < endpoints[1].x + endpoints[1].y + endpoints[1].z) {
			highest = endpoints[0];
			lowest = endpoints[1];
		} else {
			highest = endpoints[1];
			lowest = endpoints[0];
		}

}

	tf::Vector3 getX(PC::Ptr blade) {
		setEndpoints(blade);
		x.setValue(lowest.x - highest.x, lowest.y - highest.y, lowest.z - highest.z);
		return x;
	}

	tf::Vector3 getOrigin(PC::Ptr blade) {
		setEndpoints(blade);
		tf::Vector3 origin;
		origin.setValue(highest.x, highest.y, highest.z);
		return origin;
	}

	tf::Vector3 getY(PC::Ptr blade) {
		tf::Vector3 blade_normal(0, 0, 0);
		PC::Ptr temp = PC::Ptr(new PC);
		std::vector<int> indices;
		pcl::removeNaNNormalsFromPointCloud(*blade, *temp, indices);
		int size = temp->size();

		for (size_t i = 0; i < size; i++) {
			blade_normal[0] += temp->points[i].normal_x / size;
			blade_normal[1] -= temp->points[i].normal_y / size;
			blade_normal[2] -= temp->points[i].normal_z / size;
		}

		return blade_normal;
	}

	void extractPoints(PC::Ptr cloud, rs::Cluster knife) {
		pcl::PointIndices::Ptr cluster_indices(new pcl::PointIndices);
		rs::ReferenceClusterPoints clusterpoints(knife.points());
		rs::conversion::from(clusterpoints.indices(), *cluster_indices);

		for (std::vector<int>::const_iterator pit = cluster_indices->indices.begin(); pit != cluster_indices->indices.end(); pit++) {
			blade->push_back(cloud->points[*pit]);
		}
		
		pcl::VoxelGrid<PointN> filter;
		filter.setLeafSize(0.01f, 0.01f, 0.01f);
		filter.setInputCloud(blade);
		filter.filter(*blade);
	}

	void fillVisualizerWithLock(pcl::visualization::PCLVisualizer &visualizer, const bool firstRun) {
		if (firstRun) {
			visualizer.addPointCloud(cloud_r, "scene points");
		} else {
			visualizer.updatePointCloud(cloud_r, "scene points");
		}
		
		visualizer.addCone(getCoefficients(x, highest), "x");
		visualizer.setShapeRenderingProperties(pcl::visualization::PCL_VISUALIZER_COLOR, 1, 0, 0, "x");
		
		visualizer.addCone(getCoefficients(y, highest), "y");
		visualizer.setShapeRenderingProperties(pcl::visualization::PCL_VISUALIZER_COLOR, 0, 1, 0, "y");

		visualizer.addCone(getCoefficients(z, highest), "z");
		visualizer.setShapeRenderingProperties(pcl::visualization::PCL_VISUALIZER_COLOR, 0, 0, 1, "z");
	}

	pcl::ModelCoefficients getCoefficients(tf::Vector3 axis, PointN highest) {
		pcl::ModelCoefficients coeffs;
		//point
		coeffs.values.push_back(highest.x);
		coeffs.values.push_back(highest.y);
		coeffs.values.push_back(highest.z);
		//direction
		coeffs.values.push_back(axis[0]);
		coeffs.values.push_back(axis[1]);
		coeffs.values.push_back(axis[2]);
		//radius
		coeffs.values.push_back(1.0f);

		return coeffs;
	}
};

// This macro exports an entry point that is used to create the annotator.
MAKE_AE(KnifeAnnotator)
