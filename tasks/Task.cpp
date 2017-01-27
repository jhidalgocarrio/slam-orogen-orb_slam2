/* Generated from orogen/lib/orogen/templates/tasks/Task.cpp */

#include "Task.hpp"

/** Envire **/
#include <envire_core/graph/GraphViz.hpp>

/** PCL **/
#include <pcl/conversions.h>
#include <pcl/io/ply_io.h>

#define DEBUG_PRINTS 1

#ifndef D2R
#define D2R M_PI/180.00 /** Convert degree to radian **/
#endif
#ifndef R2D
#define R2D 180.00/M_PI /** Convert radian to degree **/
#endif

using namespace orb_slam2;

Task::Task(std::string const& name)
    : TaskBase(name)
{
    keyframe_point_cloud.reset(new PCLPointCloud);
    merge_point_cloud.reset(new PCLPointCloud);
}

Task::Task(std::string const& name, RTT::ExecutionEngine* engine)
    : TaskBase(name, engine)
{
    keyframe_point_cloud.reset(new PCLPointCloud);
    merge_point_cloud.reset(new PCLPointCloud);
}

Task::~Task()
{
    keyframe_point_cloud.reset();
    merge_point_cloud.reset();
}

void Task::delta_pose_samplesTransformerCallback(const base::Time &ts, const ::base::samples::RigidBodyState &delta_pose_samples_sample)
{
    #ifdef DEBUG_PRINTS
    RTT::log(RTT::Warning)<<"[ORB_SLAM2 DELTA_POSE_SAMPLES] Received time-stamp: "<<delta_pose_samples_sample.time.toMicroseconds()<<RTT::endlog();
    #endif

    Eigen::Affine3d tf_body_sensor; /** Transformer transformation **/
    /** Get the transformation Tbody_sensor **/
    if (_sensor_frame.value().compare(_body_frame.value()) == 0)
    {
        tf_body_sensor.setIdentity();
    }
    else if (!_sensor2body.get(ts, tf_body_sensor, false))
    {
        RTT::log(RTT::Fatal)<<"[ORB_SLAM2 FATAL ERROR] No transformation provided."<<RTT::endlog();
        return;
    }

    /** Set to identity if it is not initialized **/
    if (!base::isnotnan(this->tf_odo_sensor_sensor_1.matrix()))
    {
        this->tf_odo_sensor_sensor_1.setIdentity();
    }

    /** Accumulate the relative sensor to sensor transformation Tsensor_sensor(k-1) **/
    Eigen::Affine3d tf_body_body = delta_pose_samples_sample.getTransform();
    this->tf_odo_sensor_sensor_1 = this->tf_odo_sensor_sensor_1 * (tf_body_sensor.inverse() * tf_body_body.inverse() * tf_body_sensor);

    /** Need to compute a key frame image **/
    if ( this->slam->mpTracker->mState == ORB_SLAM2::Tracking::OK || this->slam->mpTracker->mState == ORB_SLAM2::Tracking::LOST)
        this->needFrame(delta_pose_samples_sample);
}

void Task::left_frameTransformerCallback(const base::Time &ts, const ::RTT::extras::ReadOnlyPointer< ::base::samples::frame::Frame > &left_frame_sample)
{
    #ifdef DEBUG_PRINTS
    std::cout << "[ORB_SLAM2 LEFT_FRAME] Frame arrived at: " <<left_frame_sample->time.toString()<< std::endl;
    #endif

    /** The image need to be in gray scale and undistorted **/
    frame_pair.first.init(left_frame_sample->size.width, left_frame_sample->size.height, left_frame_sample->getDataDepth(), base::samples::frame::MODE_GRAYSCALE);
    frameHelperLeft.convert (*left_frame_sample, frame_pair.first, 0, 0, _resize_algorithm.value(), true);

    /** Increase the computing index **/
    this->left_computing_idx++;

    /** If the difference in time is less than half of a period run the tracking **/
    base::Time diffTime = frame_pair.first.time - frame_pair.second.time;

    /** If the difference in time is less than half of a period run the tracking **/
    if (diffTime.toSeconds() < (_left_frame_period/2.0) && (this->left_computing_idx >= this->computing_counts))
    {
        frame_pair.time = frame_pair.first.time;

        #ifdef DEBUG_PRINTS
        std::cout<< "[ORB_SLAM2 LEFT_FRAME] [ON] ("<<diffTime.toMicroseconds()<<")\n";
        #endif

        /** Process the images with ORB_SLAM2 **/
        this->process(frame_pair.first, frame_pair.second, frame_pair.time);

        /** Reset computing indices **/
        this->left_computing_idx = this->right_computing_idx = 0;
    }
}

void Task::right_frameTransformerCallback(const base::Time &ts, const ::RTT::extras::ReadOnlyPointer< ::base::samples::frame::Frame > &right_frame_sample)
{
    #ifdef DEBUG_PRINTS
    std::cout<< "[ORB_SLAM2 RIGHT_FRAME] Frame arrived at: " <<right_frame_sample->time.toString()<<std::endl;
    #endif

    /** Correct distortion in image right **/
    frame_pair.second.init(right_frame_sample->size.width, right_frame_sample->size.height, right_frame_sample->getDataDepth(), base::samples::frame::MODE_GRAYSCALE);
    frameHelperRight.convert (*right_frame_sample, frame_pair.second, 0, 0, _resize_algorithm.value(), true);

    /** Increase th computing index **/
    this->right_computing_idx++;

    /** Check the time difference **/
    base::Time diffTime = frame_pair.second.time - frame_pair.first.time;

    /** If the difference in time is less than half of a period run the tracking **/
    if (diffTime.toSeconds() < (_right_frame_period/2.0) && (this->right_computing_idx >= this->computing_counts))
    {
        frame_pair.time = frame_pair.second.time;

        #ifdef DEBUG_PRINTS
        std::cout<< "[ORB_SLAM2 RIGHT_FRAME] [ON] ("<<diffTime.toMicroseconds()<<")\n";
        #endif

        /** Process the images with ORB_SLAM2 **/
        this->process(frame_pair.first, frame_pair.second, frame_pair.time);

        /** Reset computing indices **/
        this->left_computing_idx = this->right_computing_idx = 0;
    }
}

void Task::point_cloud_samplesTransformerCallback(const base::Time &ts, const ::envire::core::SpatioTemporal<pcl::PCLPointCloud2> &point_cloud_samples_sample)
{

    #ifdef DEBUG_PRINTS
    std::cout<< "[ORB_SLAM2 POINT_CLOUD] Point cloud arrived at: "<<point_cloud_samples_sample.time.toString()<<std::endl;
    #endif

    /** Convert to pcl point clouds **/
    pcl::fromPCLPointCloud2(point_cloud_samples_sample.data, *keyframe_point_cloud.get());
    std::cout<<"keyframe_point_cloud->width: "<< keyframe_point_cloud->width<<"\n";
    std::cout<<"keyframe_point_cloud->height: "<< keyframe_point_cloud->height<<"\n";
    std::cout<<"keyframe_point_cloud->size: "<< keyframe_point_cloud->size()<<"\n";

    /** Current KF transformation **/
    g2o::SE3Quat se3_kf_nav = ORB_SLAM2::Converter::toSE3Quat(this->slam->mpTracker->getLastKeyFramePose());

    Eigen::Affine3d tf_kf_cf(se3_kf_nav.rotation()); // Transformation from last keyframe to sensor frame
    tf_kf_cf.translation() = se3_kf_nav.translation();
    tf_kf_cf = tf_kf_cf * this->tf_nav_orb_sensor;

    /** Transform the point cloud in keyframe frame **/
    this->transformPointCloud(*keyframe_point_cloud, tf_kf_cf);

    /** Accumulate the cloud points **/
    *merge_point_cloud += *keyframe_point_cloud;
    keyframe_point_cloud->clear();
}

/// The following lines are template definitions for the various state machine
// hooks defined by Orocos::RTT. See Task.hpp for more detailed
// documentation about them.

bool Task::configureHook()
{
    if (! TaskBase::configureHook())
        return false;

    /** Frame index **/
    this->frame_idx = 0;

    std::cout<<"[CONFIGURATION ENVIRE_GRAPH] num_vertices: "<< this->envire_graph.num_vertices() <<"\n";
    std::cout<<"[CONFIGURATION ENVIRE_GRAPH] num_edges: "<< this->envire_graph.num_edges() <<"\n";

    /** Initial need to compute a frame is true **/
    this->flag_process_frame = true;

    /** Read the camera calibration parameters **/
    this->cameracalib = _calib_parameters.value();

    /** Frame Helper **/
    this->frameHelperLeft.setCalibrationParameter(cameracalib.camLeft);
    this->frameHelperRight.setCalibrationParameter(cameracalib.camRight);

    /** Initialize output frame **/
    ::base::samples::frame::Frame *outframe = new ::base::samples::frame::Frame();
    this->frame_out.reset(outframe);
    outframe = NULL;

    /** Set Tsensor_sensor(k-1) from odometry to Nan **/
    this->tf_odo_sensor_sensor_1.matrix()= Eigen::Matrix<double, 4, 4>::Zero() * base::NaN<double>();

    /** Set Tsensor(k-1)_sensor from ORB_SLAM2  to Identity*/
    this->tf_nav_orb_sensor.setIdentity();

    /** Optimized Output port **/
    this->slam_pose_out.invalidate();
    this->slam_pose_out.sourceFrame = _pose_samples_out_source_frame.value();

    /** Relative Frame to port out the SLAM pose samples **/
    this->slam_pose_out.targetFrame = _world_frame.value();

    RTT::log(RTT::Warning)<<"[ORB_SLAM2 TASK] DESIRED TARGET FRAME IS: "<<this->slam_pose_out.targetFrame<<RTT::endlog();

    /** Last keyframe Output port **/
    this->keyframe_pose_out.invalidate();
    this->keyframe_pose_out.sourceFrame = _keyframe_samples_out_source_frame.value();

    /** Relative Frame to port out the SLAM pose samples **/
    this->keyframe_pose_out.targetFrame = _world_frame.value();

    /* Initialize the key frame trajectory **/
    this->keyframes_trajectory.clear();
    this->allframes_trajectory.clear();

    /** Initialize the features map **/
    this->features_map.points.clear();
    this->features_map.colors.clear();

    /** Initialize the slam object **/
    this->slam.reset(new ORB_SLAM2::System(_orb_vocabulary.get(), _orb_calibration.get(), ORB_SLAM2::System::STEREO, true));

    /** Check task property parameters **/
    if (_left_frame_period.value() != _right_frame_period.value())
    {
        throw std::runtime_error("[ORB_SLAM2] Input port period in Left and Right images must be equal!");
    }

    if (_desired_period.value() < _left_frame_period.value())
    {
        throw std::runtime_error("[ORB_SLAM2] Desired period cannot be smaller than input ports period!");
    }
    else if (_desired_period.value() == 0.00)
    {
        _desired_period.value() = _left_frame_period.value();
        this->computing_counts = 1;
    }
    else
    {
        this->computing_counts = boost::math::iround(_desired_period.value()/_left_frame_period.value());
        _desired_period.value() = this->computing_counts * _left_frame_period.value();
    }

    RTT::log(RTT::Warning)<<"[ORB_SLAM2] Actual Computing Period: "<<_desired_period.value()<<" [seconds]"<<RTT::endlog();

    this->left_computing_idx = this->right_computing_idx = 0;

    return true;
}

bool Task::startHook()
{
    if (! TaskBase::startHook())
        return false;
    return true;
}

void Task::updateHook()
{
    TaskBase::updateHook();
}

void Task::errorHook()
{
    TaskBase::errorHook();
}

void Task::stopHook()
{
    TaskBase::stopHook();
}

void Task::cleanupHook()
{
    TaskBase::cleanupHook();

    /** Save the keyframes trajectory in text format (translation + heading) **/
    this->saveKFTrajectoryText("orb_slam2_keyframes_trajectory.data");

    /** Save the all frames trajectory in text format (translation + heading) **/
    this->saveAllTrajectoryText("orb_slam2_allframes_trajectory.data");

    /** Save envire_graph dot image **/
    envire::core::GraphViz viz;
    viz.write(this->envire_graph, "envire_graph_orb_slam2_graphviz.dot");

    /** Create the map point cloud **/
    this->mergePointClouds(*merge_point_cloud.get());

    /** Save the map point cloud to file **/
    pcl::io::savePLYFileBinary (_output_ply.value(), *merge_point_cloud.get());

    /** Reset keyframe trajectory out port**/
    this->keyframes_trajectory.clear();
    this->allframes_trajectory.clear();

    /** Stop ORB_SLAM2 all threads **/
    this->slam->Shutdown();

    /** Reset ORB_SLAM2 **/
    this->slam.reset();
}

void Task::process(const base::samples::frame::Frame &frame_left,
                const base::samples::frame::Frame &frame_right,
                const base::Time &timestamp)
{
    /** Convert Images to opencv **/
    cv::Mat img_l = frameHelperLeft.convertToCvMat(frame_left);
    cv::Mat img_r = frameHelperRight.convertToCvMat(frame_right);

    /** Check whether there is delta pose in camera frame from motion model **/
    cv::Mat tf_motion_model;
    if (base::isnotnan(this->tf_odo_sensor_sensor_1.matrix()))
    {
        //std::cout<<"[ORB_SLAM2 PROCESS] TF_ODO_SENSOR_SENSOR_1:\n"<< this->tf_odo_sensor_sensor_1.matrix() <<"\n";

        /** ORB_SLAM2 with motion model information **/
        tf_motion_model = ORB_SLAM2::Converter::toCvMat(this->tf_odo_sensor_sensor_1.matrix());

        /** Reset the Tsensor(k)_sensor(k-1) **/
        this->tf_odo_sensor_sensor_1.setIdentity();
    }

    /** ORB_SLAM2 **/
    this->slam->TrackStereo(img_l, img_r, timestamp.toMilliseconds(), tf_motion_model);

    /** Set insert frame to false **/
    this->flag_process_frame = true;

    /** Left color image **/
    if (_output_debug.get())
    {
        /** Get frame with information from ORB_SLAM2 **/
        cv::Mat img_out = this->slam->mpFrameDrawer->DrawFrame();

        /** Convert to Frame **/
        ::base::samples::frame::Frame *frame_ptr = this->frame_out.write_access();
        this->frameHelperLeft.copyMatToFrame(img_out, *frame_ptr);

        /** Out port the image **/
        frame_ptr->time = this->frame_pair.time;
        this->frame_out.reset(frame_ptr);
        _frame_samples_out.write(this->frame_out);
    }

    /** Get the transformation Tworld_navigation **/
    Eigen::Affine3d tf_world_nav; /** Transformer transformation **/
    /** Get the transformation Tworld_navigation (navigation is body_0) **/
    if (_navigation_frame.value().compare(_world_frame.value()) == 0)
    {
        tf_world_nav.setIdentity();
    }
    else if (!_navigation2world.get(timestamp, tf_world_nav, false))
    {
        RTT::log(RTT::Fatal)<<"[ORB_SLAM2 FATAL ERROR]  No transformation provided."<<RTT::endlog();
       return;
    }

    Eigen::Affine3d tf_body_sensor; /** Transformer transformation **/
    /** Get the transformation Tbody_sensor **/
    if (_sensor_frame.value().compare(_body_frame.value()) == 0)
    {
        tf_body_sensor.setIdentity();
    }
    else if (!_sensor2body.get(timestamp, tf_body_sensor, false))
    {
        RTT::log(RTT::Fatal)<<"[ORB_SLAM2 FATAL ERROR] No transformation provided."<<RTT::endlog();
       return;
    }

    /** Get the camera pose from ORB_SLAM2 **/
    if (!this->slam->mpTracker->mCurrentFrame.mTcw.empty())
    {
        g2o::SE3Quat se3_nav_sensor = ORB_SLAM2::Converter::toSE3Quat(this->slam->mpTracker->mCurrentFrame.mTcw).inverse();

        /** SE3 to Affine3d **/
       this->tf_nav_orb_sensor = se3_nav_sensor.rotation();
       this->tf_nav_orb_sensor.translation() = se3_nav_sensor.translation();
    }

    /** Tworld_body = Tworld_body * Tbody_sensor * Tsensor(k-1)_sensor * Tsensor_body **/
    Eigen::Affine3d tf_world_body = tf_world_nav * tf_body_sensor * this->tf_nav_orb_sensor * tf_body_sensor.inverse();

    /** Out port the last slam pose **/
    this->slam_pose_out.time = timestamp;
    this->slam_pose_out.setTransform(tf_world_body);
    _pose_samples_out.write(this->slam_pose_out);

    /** Get the trajectory of key frames **/
    this->getFramesPose(this->keyframes_trajectory, this->allframes_trajectory, Eigen::Affine3d(tf_world_nav * tf_body_sensor));
    _keyframes_trajectory_out.write(this->keyframes_trajectory);
    _allframes_trajectory_out.write(this->allframes_trajectory);

    /** Out port the last keyframe pose **/
    this->keyframe_pose_out.time = timestamp;
    _keyframe_pose_samples_out.write(this->keyframe_pose_out);

    /** Get the features map **/
    //this->getMapPointsPose(this->features_map, Eigen::Affine3d(tf_world_nav * tf_body_sensor));
    //this->features_map.time = timestamp;
    //_features_map_out.write(this->features_map);

    /** Check if a Keyframe is inserted **/
    if (this->slam->mpTracker->new_key_frame_inserted)
    {
        /** Get frame id **/
        std::string frame_id = std::to_string(this->slam->mpTracker->getLastKeyFrameId());
        std::cout<<"[ORB_SLAM2 PROCESS ENVIRE_GRAPH] Frame_id: "<< frame_id <<"\n";

        if (this->envire_graph.num_vertices() ==  0)
        {
            /** Store the name of the first Keyframe **/
            this->first_kf_id = frame_id;
            std::cout<<"[ORB_SLAM2 PROCESS ENVIRE_GRAPH] FIRST_KEYFRAME_ID: "<< this->first_kf_id <<"\n";
        }

        /** Add frame in envire graph **/
        this->envire_graph.addFrame(frame_id);

        /** Add transformation to the graph **/
        envire::core::Transform tf(timestamp, ::base::TransformWithCovariance(this->keyframe_pose_out.getTransform()));
        this->envire_graph.addTransform(this->first_kf_id, frame_id, tf);

        std::cout<<"[ORB_SLAM2 PROCESS ENVIRE_GRAPH] PUSH merge_point_cloud.size(): "<< this->merge_point_cloud->size() <<"\n";

        if (this->merge_point_cloud->size() > 0)
        {
            /** Add item to frame **/
            PointCloudItem::Ptr point_cloud_item(new PointCloudItem);
            point_cloud_item->setData(*(this->merge_point_cloud));
            this->envire_graph.addItemToFrame(frame_id, point_cloud_item);
        }

        std::cout<<"[ORB_SLAM2 PROCESS ENVIRE_GRAPH] num_vertices: "<< this->envire_graph.num_vertices() <<"\n";
        std::cout<<"[ORB_SLAM2 PROCESS ENVIRE_GRAPH] num_edges: "<< this->envire_graph.num_edges() <<"\n";

        /** Write the point cloud into the port **/
        ::envire::core::SpatioTemporal<pcl::PCLPointCloud2> point_cloud_out;
        pcl::toPCLPointCloud2(*merge_point_cloud.get(), point_cloud_out.data);
        point_cloud_out.time = timestamp;
        _point_cloud_samples_out.write(point_cloud_out);

        /** Clear accumulated point cloud in key frame **/
        this->merge_point_cloud->clear();
        std::cout<<"[ORB_SLAM2 PROCESS ENVIRE_GRAPH] CLEAN merge_point_cloud.size(): "<< this->merge_point_cloud->size() <<"\n";
    }

    /** Update the envire graph with the optimized transformation values **/
    this->updateEnvireGraph(Eigen::Affine3d(tf_world_nav * tf_body_sensor));

    /** Write in the slam information port **/
    this->info.time = timestamp;
    this->info.number_relocalizations = this->slam->mpTracker->number_relocalizations;
    this->info.number_loops = this->slam->mpLoopCloser->number_loops;
    this->info.fps = (1.0/_left_frame_period.value())/this->computing_counts;
    _task_info_out.write(this->info);
}

void Task::needFrame (const ::base::samples::RigidBodyState &delta_pose_samples)
{
    std::cout<<"[ORB_SLAM2 NEED_KEYFRAME] COV_VELOCITY:\n"<<delta_pose_samples.cov_velocity(0,0)<<" "<<delta_pose_samples.cov_velocity(1,1)<<" "<< delta_pose_samples.cov_velocity(2,2) <<"\n";
    std::cout<<"[ORB_SLAM2 NEED_KEYFRAME] NORM_COV_VELOCITY:\n"<<delta_pose_samples.cov_velocity.norm() <<"\n";
    std::cout<<"[ORB_SLAM2 NEED_KEYFRAME] SUM_COV_VELOCITY:\n"<<delta_pose_samples.cov_velocity.sum() <<"\n";
    double residual = delta_pose_samples.cov_velocity.norm();
    unsigned short new_computing_counts;

    if (residual < _velocity_residual_threshold.value())
    {
        /** Smaller than threshold reduce the frequency **/
        new_computing_counts = boost::math::iround(2.0 * _desired_period.value()/_left_frame_period.value());
        std::cout<<"[ORB_SLAM2 NEED_KEYFRAME] NEW COUNTING COUNTS:\n"<<new_computing_counts <<"\n";
    }
    else if (residual > 2.0 * _velocity_residual_threshold.value())
    {
        /** Bigger than double the threshold reduce the frequency **/
        new_computing_counts = boost::math::iround(0.3 * _desired_period.value()/_left_frame_period.value());
        std::cout<<"[ORB_SLAM2 NEED_KEYFRAME] NEW COUNTING COUNTS:\n"<<new_computing_counts <<"\n";
    }
    else
    {
        new_computing_counts = boost::math::iround(_desired_period.value()/_left_frame_period.value());
        std::cout<<"[ORB_SLAM2 NEED_KEYFRAME] NEW COUNTING COUNTS:\n"<<new_computing_counts <<"\n";
    }

    if (this->flag_process_frame)
    {
        /** Frame has been processed **/
        this->flag_process_frame = false;
        this->computing_counts = new_computing_counts;
        std::cout<<"[ORB_SLAM2 NEED_KEYFRAME] COUNTING COUNTS CHANGED, KEYFRAME PROCESSED!!\n";
    }
    else if (new_computing_counts < this->computing_counts)
    {
        /** KeyFrame has been processed **/
        this->computing_counts = new_computing_counts;
        std::cout<<"[ORB_SLAM2 NEED_KEYFRAME] COUNTING COUNTS CHANGED, BECAUSE SMALLER!!\n";
    }
    return;
}

void Task::getFramesPose( std::vector< ::base::Waypoint > &kf_trajectory, std::vector< ::base::Waypoint > &frames_trajectory, const Eigen::Affine3d &tf)
{
    /** Clear trajectory **/
    kf_trajectory.clear();

    /** Get the key frames and sort them by id **/
    std::vector< ::ORB_SLAM2::KeyFrame* > vpKFs = this->slam->mpMap->GetAllKeyFrames();
    std::sort(vpKFs.begin(),vpKFs.end(), ::ORB_SLAM2::KeyFrame::lId);

    // Transform all keyframes so that the first keyframe is at the origin.
    // After a loop closure the first keyframe might not be at the origin.
    cv::Mat Two = vpKFs[0]->GetPoseInverse();

    /********************************
     * Store the last keyframe pose 
    *********************************/
    if (vpKFs.size() > 0)
    {
        cv::Mat Tcw = vpKFs[vpKFs.size()-1]->GetPose()*Two;
        cv::Mat Rwc = Tcw.rowRange(0,3).colRange(0,3).t();
        cv::Mat twc = -Rwc*Tcw.rowRange(0,3).col(3);

        /** ORB_SLAM2 quaternion is x, y, z, w and Eigen quaternion is w, x, y, z **/
        std::vector<float> q = ORB_SLAM2::Converter::toQuaternion(Rwc);
        ::base::Pose pose (tf * ::base::Pose(ORB_SLAM2::Converter::toVector3d(twc), ::base::Orientation(q[3], q[0], q[1], q[2])).toTransform());

        this->keyframe_pose_out.setTransform(pose.toTransform());
    }

    /*********************************
    * Store the Keyframes trajectory
    *********************************/
    for(std::vector< ::ORB_SLAM2::KeyFrame* >::iterator it = vpKFs.begin(); it != vpKFs.end(); ++it)
    {
        /** Get the transformation world to keyframe **/
        cv::Mat Tcw = (*it)->GetPose()*Two;
        cv::Mat Rwc = Tcw.rowRange(0,3).colRange(0,3).t();
        cv::Mat twc = -Rwc*Tcw.rowRange(0,3).col(3);

        /** ORB_SLAM2 quaternion is x, y, z, w and Eigen quaternion is w, x, y, z **/
        std::vector<float> q = ORB_SLAM2::Converter::toQuaternion(Rwc);
        ::base::Pose pose (tf * ::base::Pose(ORB_SLAM2::Converter::toVector3d(twc), ::base::Orientation(q[3], q[0], q[1], q[2])).toTransform());

        /** Store the pose in the trajectory **/
        kf_trajectory.push_back(base::Waypoint(pose.position, pose.getYaw(), 0.0, 0.0));
    }

    // Frame pose is stored relative to its reference keyframe (which is optimized by BA and pose graph).
    // We need to get first the keyframe pose and then concatenate the relative transformation.
    // Frames not localized (tracking failure) are not saved.

    frames_trajectory.clear();
    // For each frame we have a reference keyframe (lRit), the timestamp (lT) and a flag
    // which is true when tracking failed (lbL).
    std::list< ::ORB_SLAM2::KeyFrame*>::iterator lRit = this->slam->mpTracker->mlpReferences.begin();
    std::list<double>::iterator lT = this->slam->mpTracker->mlFrameTimes.begin();
    std::list<bool>::iterator lbL = this->slam->mpTracker->mlbLost.begin();
    for(std::list<cv::Mat>::iterator lit=this->slam->mpTracker->mlRelativeFramePoses.begin(),
        lend=this->slam->mpTracker->mlRelativeFramePoses.end();lit!=lend;lit++, lRit++, lT++, lbL++)
    {
        if(*lbL)
            continue;

        ::ORB_SLAM2::KeyFrame* pKF = *lRit;

        cv::Mat Trw = cv::Mat::eye(4,4,CV_32F);

        // If the reference keyframe was culled, traverse the spanning tree to get a suitable keyframe.
        while(pKF->isBad())
        {
            Trw = Trw*pKF->mTcp;
            pKF = pKF->GetParent();
        }

        Trw = Trw*pKF->GetPose()*Two;

        cv::Mat Tcw = (*lit)*Trw;
        cv::Mat Rwc = Tcw.rowRange(0,3).colRange(0,3).t();
        cv::Mat twc = -Rwc*Tcw.rowRange(0,3).col(3);

        /** ORB_SLAM2 quaternion is x, y, z, w and Eigen quaternion is w, x, y, z **/
        std::vector<float> q = ORB_SLAM2::Converter::toQuaternion(Rwc);
        ::base::Pose pose (tf * ::base::Pose(ORB_SLAM2::Converter::toVector3d(twc), ::base::Orientation(q[3], q[0], q[1], q[2])).toTransform());

        /** Store the pose in the trajectory **/
        frames_trajectory.push_back(base::Waypoint(pose.position, pose.getYaw(), 0.0, 0.0));
    }
    return;
}


void Task::getMapPointsPose( ::base::samples::Pointcloud &points_map,  const Eigen::Affine3d &tf)
{
    /** Clean point cloud **/
    points_map.points.clear();

    const std::vector< ORB_SLAM2::MapPoint* > &vpMPs = this->slam->mpMap->GetAllMapPoints();
    const std::vector< ORB_SLAM2::MapPoint* > &vpRefMPs = this->slam->mpMap->GetReferenceMapPoints();

    std::set<ORB_SLAM2::MapPoint*> spRefMPs(vpRefMPs.begin(), vpRefMPs.end());

    if(vpMPs.empty())
        return;

    //for(size_t i=0, iend=vpMPs.size(); i<iend;i++)
    //{
    //    if(vpMPs[i]->isBad() || spRefMPs.count(vpMPs[i]))
    //        continue;
    //    g2o::SE3Quat pos = ORB_SLAM2::Converter::toSE3Quat(vpMPs[i]->GetWorldPos());
    //    Eigen::Affine3d tf_point; tf_point = pos.rotation();
    //    tf_point.translation() = pos.translation();
    //    points_map.points.push_back((tf * tf_point).translation());
    //}

    for(std::set<ORB_SLAM2::MapPoint*>::iterator sit=spRefMPs.begin(),
            send=spRefMPs.end(); sit!=send; sit++)
    {
        if((*sit)->isBad())
            continue;
        g2o::SE3Quat pos = ORB_SLAM2::Converter::toSE3Quat((*sit)->GetWorldPos());
        Eigen::Affine3d tf_point; tf_point = pos.rotation();
        tf_point.translation() = pos.translation();
        tf_point = tf * tf_point;
        points_map.points.push_back(tf_point.translation());
    }

    return;
}
void Task::saveKFTrajectoryText(const string &filename, const Eigen::Affine3d &tf)
{
    std::cout << std::endl << "Saving camera trajectory to " << filename << " ..." << std::endl;

    std::ofstream f;
    f.open(filename.c_str());
    f << std::fixed;

    for (std::vector< ::base::Waypoint >::iterator it = this->keyframes_trajectory.begin();
            it != this->keyframes_trajectory.end(); ++it)
    {
        f << std::setprecision(9) << it->position[0] << " " << it->position[1] << " " << it->position[2] << " " << it->heading << std::endl;
    }

    f.close();
    std::cout << std::endl << "trajectory saved with "<<this->keyframes_trajectory.size()<<" KFs!" << std::endl;

    return;
}

void Task::saveAllTrajectoryText(const string &filename, const Eigen::Affine3d &tf)
{
    std::cout << std::endl << "Saving camera trajectory to " << filename << " ..." << std::endl;

    std::ofstream f;
    f.open(filename.c_str());
    f << std::fixed;

    for (std::vector< ::base::Waypoint >::iterator it = this->allframes_trajectory.begin();
            it != this->allframes_trajectory.end(); ++it)
    {
        f << std::setprecision(9) << it->position[0] << " " << it->position[1] << " " << it->position[2] << " " << it->heading << std::endl;
    }

    f.close();
    std::cout << std::endl << "trajectory saved with "<<this->allframes_trajectory.size()<<" frames!" << std::endl;

    return;
}

void Task::transformPointCloud(pcl::PointCloud<PointType> &pcl_pc, const Eigen::Affine3d& transformation)
{
    for(std::vector< PointType, Eigen::aligned_allocator<PointType> >::iterator it = pcl_pc.begin(); it != pcl_pc.end(); it++)
    {
        Eigen::Vector3d point (it->x, it->y, it->z);
        point = transformation * point;
        PointType pcl_point;
        pcl_point.x = point[0]; pcl_point.y = point[1]; pcl_point.z = point[2];
        pcl_point.rgb = it->rgb;
        *it = pcl_point;
    }
}

void Task::downsample (const PCLPointCloudPtr &points, const ::base::Vector3d &leaf_size, PCLPointCloudPtr &downsampled_out)
{

    pcl::VoxelGrid<PointType> vox_grid;
    vox_grid.setLeafSize (leaf_size[0], leaf_size[1], leaf_size[2]);
    vox_grid.setInputCloud (points);
    vox_grid.filter (*downsampled_out);

    return;
}

PCLPointCloud &Task::getPointCloud(const std::string &frame_id)
{
    try
    {
        /** Get Item return an iterator to the first element **/
        orb_slam2::PointCloudItem &point_cloud_item = *(this->envire_graph.getItem<orb_slam2::PointCloudItem>(frame_id));
        return point_cloud_item.getData();
    }catch(envire::core::UnknownFrameException &ufex)
    {
        std::cerr << ufex.what() << std::endl;
        throw "[ORB_SLAM2] Envire Graph: getPointCloud() point cloud not found\n";
    }
}

void Task::updateEnvireGraph(const Eigen::Affine3d &tf)
{
    std::cout<<"[ORB_SLAM2 UPDATE ENVIRE_GRAPH]:\n";

    /** Update the envire_graph transformation **/
    std::vector< ::ORB_SLAM2::KeyFrame* > vpKFs = this->slam->mpMap->GetAllKeyFrames();
    std::sort(vpKFs.begin(),vpKFs.end(), ::ORB_SLAM2::KeyFrame::lId);
    cv::Mat Two = vpKFs[0]->GetPoseInverse();

    std::cout<<"first_kf_id:"<<this->first_kf_id<<" vpKFs[0]:"<< vpKFs[0]->mnId <<"\n";

    for(std::vector< ::ORB_SLAM2::KeyFrame* >::iterator it = vpKFs.begin(); it != vpKFs.end(); ++it)
    {
        /** Get the transformation world to keyframe **/
        cv::Mat Tcw = (*it)->GetPose()*Two;
        cv::Mat Rwc = Tcw.rowRange(0,3).colRange(0,3).t();
        cv::Mat twc = -Rwc*Tcw.rowRange(0,3).col(3);

        /** ORB_SLAM2 quaternion is x, y, z, w and Eigen quaternion is w, x, y, z **/
        std::vector<float> q = ORB_SLAM2::Converter::toQuaternion(Rwc);
        Eigen::Affine3d tf_pose (tf * ::base::Pose(ORB_SLAM2::Converter::toVector3d(twc), ::base::Orientation(q[3], q[0], q[1], q[2])).toTransform());

        /** Update the transformation in the envire graph **/
        std::string frame_id = std::to_string((*it)->mnId);
        envire::core::Transform envire_tf = this->envire_graph.getTransform(this->first_kf_id, frame_id);
        envire_tf.setTransform(::base::TransformWithCovariance(tf_pose));
        this->envire_graph.updateTransform(this->first_kf_id, frame_id, envire_tf);
    }

}

void Task::mergePointClouds(PCLPointCloud &map_point_cloud)
{
    map_point_cloud.clear();

    std::cout<<"[ORB_SLAM2 MERGE POINT CLOUDS]:\n";

    /** Merge the point cloud **/
    std::pair<envire::core::EnvireGraph::vertex_iterator, envire::core::EnvireGraph::vertex_iterator> vp;
    for (vp = this->envire_graph.getVertices(); vp.first != vp.second; ++vp.first)
    {
        std::string frame_id = this->envire_graph.getFrameId(*(vp.first));
        std::cout<<"id: "<<frame_id<<" ";

        if (this->envire_graph.containsItems<orb_slam2::PointCloudItem>(frame_id))
        {
            PCLPointCloud local_points = this->getPointCloud(frame_id);
            base::TransformWithCovariance tf_cov = this->envire_graph.getTransform(this->first_kf_id, frame_id).transform;
            this->transformPointCloud(local_points, tf_cov.getTransform());
            map_point_cloud += local_points;
            std::cout<<"local_points.size(); "<<local_points.size()<<"\n";
        }
    }

    /** Downsample the map point cloud **/
    PCLPointCloudPtr map_point_cloud_ptr = boost::make_shared<PCLPointCloud>(map_point_cloud);
    PCLPointCloudPtr downsample_point_cloud (new PCLPointCloud);
    this->downsample (map_point_cloud_ptr, _map_point_cloud_resolution.value(), downsample_point_cloud);

    map_point_cloud = *downsample_point_cloud;
    downsample_point_cloud.reset();
    return;
}


