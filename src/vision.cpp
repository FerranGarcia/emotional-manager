/*
    This example program shows how to find frontal human faces in an image and
    estimate their pose.  The pose takes the form of 68 landmarks. These are
    points on the face such as the corners of the mouth, along the eyebrows, on
    the eyes, and so forth.
*/

#include <dlib/opencv.h>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/video/tracking.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/calib3d/calib3d.hpp>

#include <dlib/image_processing/frontal_face_detector.h>
#include <dlib/image_processing/render_face_detections.h>

#include <dlib/gui_widgets.h>

#include "ros/ros.h"
#include "std_msgs/String.h"
#include "std_msgs/Int16.h"
#include "std_msgs/Empty.h"
#include "std_msgs/Float32.h"

#include <sstream>
#include <vector>
#include <cmath>
#include <iostream>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string>
#include <thread>

/*
#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/freeglut.h>
*/

#define MAX_COUNT 100
char imageFileName[32];
long imageIndex = 0;
char keyPressed;
bool contact = true;

using namespace dlib;
using namespace std;

float t = 1;
std::vector<float> EMA(6,1);

int look_right_counter = 0;
int look_left_counter = 0;
int look_up_counter = 0;
int look_down_counter = 0;
int smile_counter = 0;
int prevSize = 0;
bool new_child = false;
string state = " ";
image_window win;

// Create a dictionary for the markers.
std::map<string, int> partToPoint = {
    {"nose", 30},
    {"right_side", 2},
    {"left_side", 14},
    {"eyebrow_right", 21},
    {"eyebrow_left", 22},
    {"mouth_up", 51},
    {"mouth_down", 57},
    {"mouth_right", 48},
    {"mouth_left", 54}
};


double rot[9] = {0};
std::vector<double> rv(3);
std::vector<double> tv(3);
cv::Mat rvec(rv);
cv::Mat tvec(tv);
cv::Mat camMatrix;
cv::Mat op;


double tt_tic=0;

void tic(){
    tt_tic = cv::getTickCount();
}
void toc(){
    double tt_toc = (cv::getTickCount() - tt_tic)/(cv::getTickFrequency());
    printf ("toc: %4.3f sn\n", tt_toc);
}

// Get current date/time, format is YYYY-MM-DD.HH:mm:ss
const std::string currentDateTime() {
    time_t     now = time(0);
    struct tm  tstruct;
    char       buf[80];
    tstruct = *localtime(&now);
    strftime(buf, sizeof(buf), "%Y-%m-%d_%X", &tstruct);

    return buf;
}

// Return the point correspondent to the dictionary marker.
cv::Point2f getPointFromPart(shape_predictor &pose_model, cv_image<bgr_pixel> &cimg, rectangle face, string name){
    cv::Point2f point;
    point.x = pose_model(cimg, face).part(partToPoint[name]).x();
    point.y = pose_model(cimg, face).part(partToPoint[name]).y();

    return point;
}

// Returns 1 if the lines intersect, otherwise 0.
bool get_line_intersection(float p0_x, float p0_y, float p1_x, float p1_y,
                           float p2_x, float p2_y, float p3_x, float p3_y){
    float s1_x, s1_y, s2_x, s2_y;
    s1_x = p1_x - p0_x;     s1_y = p1_y - p0_y;
    s2_x = p3_x - p2_x;     s2_y = p3_y - p2_y;

    float s, t;
    s = (-s1_y * (p0_x - p2_x) + s1_x * (p0_y - p2_y)) / (-s2_x * s1_y + s1_x * s2_y);
    t = ( s2_x * (p0_y - p2_y) - s2_y * (p0_x - p2_x)) / (-s2_x * s1_y + s1_x * s2_y);

    if (s >= 0 && s <= 1 && t >= 0 && t <= 1){
        return 1;
    }
    return 0;
}

// Computes the size of the head consideing the vertical and horizontal segments
void sizeHead(ros::Publisher sizeHead_pub, shape_predictor &pose_model, cv_image<bgr_pixel> &cimg, rectangle face){
    // Get up
    cv::Point2f eyebrow_right = getPointFromPart(pose_model, cimg, face, "eyebrow_right");
    cv::Point2f eyebrow_left = getPointFromPart(pose_model, cimg, face, "eyebrow_left");
    cv::Point2f up;
    up.x = (eyebrow_right.x + eyebrow_left.x);
    up.y = (eyebrow_right.y + eyebrow_left.y);

    // Get right
    cv::Point2f right = getPointFromPart(pose_model, cimg, face, "right_side");
    // Get left
    cv::Point2f left = getPointFromPart(pose_model, cimg, face, "left_side");

    // Compute size of the head
    float horizontal = sqrt((right.x-left.x)*(right.x-left.x) + (right.y-left.y)*(right.y-left.y));
    float vertical = sqrt((right.x-up.x)*(right.x-up.x) + (right.y-up.y)*(right.y-up.y));
    auto size = int((vertical*horizontal)/1000);

    if (abs(prevSize - size)> 5){
        cout <<"Head size:"<< size << endl;
        std_msgs::Int16 msgSizeHead;
        msgSizeHead.data = size;
        sizeHead_pub.publish(msgSizeHead);
        prevSize = size;
    }
}

/*
 Detects if someone smiled to the robot. I would be better to
 use Haar detector from openCV depite the computational cost
*/
void smileDetector(ros::Publisher smile_pub, shape_predictor &pose_model, cv_image<bgr_pixel> &cimg, rectangle face){

    // Get mouth
    cv::Point2f mouth_up = getPointFromPart(pose_model, cimg, face, "mouth_up");
    cv::Point2f mouth_down = getPointFromPart(pose_model, cimg, face, "mouth_down");
    cv::Point2f mouth_right = getPointFromPart(pose_model, cimg, face, "mouth_right");
    cv::Point2f mouth_left = getPointFromPart(pose_model, cimg, face, "mouth_left");

    char intersec = 0;
    intersec = get_line_intersection(mouth_left.x, mouth_left.y, mouth_right.x, mouth_right.y,
                                     mouth_up.x, mouth_up.y, mouth_down.x, mouth_down.y);

    if (intersec == 0){
        if (contact==true){
            smile_counter = smile_counter +1;
            if(smile_counter>5){
                cout <<"Someone smiled"<< endl;
                std_msgs::Empty msgEmpty;
                smile_pub.publish(msgEmpty);
                smile_counter = 0;
            }
        }
    }
}

/* To compute the saliency or novelty, it is necessary to consider all the other features
 * into the EMA value X[]. If the value is greater than the previous EMA with a certain
 * threshold, a novelty has been detected
 */
void novelty(ros::Publisher novelty_pub, std::vector<bool> lookAt,
             std::vector<rectangle> faces, float mu, float eps, float threshold){

    std::vector<float> X(6,0);
    X[0] = float(lookAt[0]);
    X[1] = float(lookAt[1]);
    X[2] = float(lookAt[2]);
    X[3] = float(lookAt[3]);
    X[4] = float(contact);
    X[5] = float(faces.size());

    std::vector<float> temp = EMA;

    for (unsigned int j = 0; j < EMA.size(); ++j){
        EMA[j] = mu*X[j] + (1-mu)*EMA[j];
    }

    float dist = 0.0;

    for (unsigned int j = 0; j < EMA.size(); ++j){
        dist += 2*(EMA[j]-temp[j])*(EMA[j]-temp[j]) / ( eps+(EMA[j]+temp[j])*(EMA[j]+temp[j]));
    }
    if( dist>threshold){
        std_msgs::Float32 msgNovelty;
        cout <<"Novelty detected! :"<< dist*sqrt(t) << endl;
        msgNovelty.data = dist*sqrt(t);
        novelty_pub.publish(msgNovelty);
        t = 1;
    }
    else{
        t++;
    }
}

//TODO: Simplify code in the message sending
std::vector<bool> lookAt(ros::Publisher lookAt_pub, shape_predictor &pose_model, cv_image<bgr_pixel> &cimg,
                         rectangle face, std::vector<full_object_detection> &contacts, cv::Mat &rgbFrames){
    std::vector<bool> lookAt(4);
    // Get nose
    cv::Point2f nose = getPointFromPart(pose_model, cimg, face, "nose");

    /*float nose.x = pose_model(cimg, faces[i]).part(30).x();
    float nose.y = pose_model(cimg, faces[i]).part(30).y();
    noses.push_back(centered_rect(point(nose.x,nose.y),8,8));*/

    //get rights
    cv::Point2f right = getPointFromPart(pose_model, cimg, face, "right_side");
    //get lefts
    cv::Point2f left = getPointFromPart(pose_model, cimg, face, "left_side");

    float horRight = sqrt((right.x-nose.x)*(right.x-nose.x) + (right.y-nose.y)*(right.y-nose.y));
    float horLeft = sqrt((left.x-nose.x)*(left.x-nose.x) + (left.y-nose.y)*(left.y-nose.y));
    float horizontal = sqrt((right.x-left.x)*(right.x-left.x) + (right.y-left.y)*(right.y-left.y));
    auto estwest = (horRight-horLeft)/horizontal; // -1<score<1

    float l = horRight*horizontal/(horRight+horLeft);
    float xl = right.x+(left.x-right.x)*l/horizontal;
    float yl = right.y+(left.y-right.y)*l/horizontal;

    float sh = (yl-nose.y)/abs(nose.y-yl);
    float h = sqrt((nose.y-yl)*(nose.y-yl) + (nose.x-xl)*(nose.x-xl));
    float southnorth = sh*h/horRight;


    /*//hack for southnord between -1 and 1 :
    if(southnorth>0){
        southnorth = southnorth/0.45;
    }
    if(southnorth<0){
        southnorth = southnorth/0.37;
    }

    southnorth = (3.1415/4)*southnorth;
    estwest = (3.1415/3)*estwest;
    cout<< "vert: "<< southnorth << " horz: "<<estwest<<endl;*/
    //Now, southnord and estwest are radian angles corresponding to the angle of head
    //angle 0,0 => facing camera
    //angle pi/3,0 => look left
    //angle 0,pi/3 => look up

    //Pass to degrees
    estwest = (estwest*180)/3.1415;
    southnorth = (southnorth*180)/3.1415;
    //compute the vector
    float x = cos(estwest)*cos(southnorth);
    float y = sin(estwest)*cos(southnorth);
    float z = sin(southnorth);

    double mult =100;
    //cv::Point2f nose_ax = nose + cv::Point2f(x,y)*mult;
    //cv::line(rgbFrames, nose, nose_ax, cv::Scalar(255, 0, 0), 8, 1, 0);


    bool look_left = estwest>0.3;
    bool look_right = estwest<-0.3;
    bool look_up = southnorth>0.3;
    bool look_down = southnorth<-0.3;

    contact = true;

    std_msgs::String msg;
    std::stringstream ss;

    if(look_right){
        smile_counter = 0;
        look_right_counter = look_right_counter + 1;
        if (look_right_counter > 5){
            ss << /*"face " << i <<*/ "right";
            msg.data = ss.str();
            ROS_INFO("%s", msg.data.c_str());
            lookAt_pub.publish(msg);
            contact=false;
            look_right_counter = 0;
        }
    }
    if(look_left){
        smile_counter = 0;
        look_left_counter = look_left_counter + 1;
        if (look_left_counter > 5){
            ss << /*"face " << i <<*/ "left";
            msg.data = ss.str();
            ROS_INFO("%s", msg.data.c_str());
            lookAt_pub.publish(msg);
            contact=false;
            look_left_counter = 0;
        }
    }
    if(look_up){
        smile_counter = 0;
        look_up_counter = look_up_counter + 1;
        if (look_up_counter > 5){
            ss << /*"face " << i <<*/ "robot contact";
            msg.data = ss.str();
            ROS_INFO("%s", msg.data.c_str());
            lookAt_pub.publish(msg);
            contact=false;
            look_up_counter = 0;
        }
    }
    if(look_down){
        smile_counter = 0;
        look_down_counter = look_down_counter + 1;
        if(look_down_counter > 5){
            ss << /*"face " << i <<*/ "down";
            msg.data = ss.str();
            ROS_INFO("%s", msg.data.c_str());
            lookAt_pub.publish(msg);
            contact=false;
            look_down_counter = 0;
        }
    }
    if(contact){
        //cout << "contact !!" << endl;
        contacts.push_back(pose_model(cimg, face));
    }
    lookAt[0] = look_right;
    lookAt[1] = look_left;
    lookAt[2] = look_up;
    lookAt[3] = look_down;

    return lookAt;
}

void amountMovement(ros::Publisher movement_pub, cv::Mat &rgbFrames, cv::Mat &grayFrames, cv::Mat &prevGrayFrame,
                    cv::Mat &opticalFlow, std::vector<cv::Point2f> &points1, std::vector<cv::Point2f> &points2, bool &needToInit){

    std::vector<uchar> status;
    std::vector<float> err;

    cv::TermCriteria termcrit(CV_TERMCRIT_ITER | CV_TERMCRIT_EPS, 20, 0.03);
    cv::Size winSize(31, 31);

    if (needToInit) {
        cv::goodFeaturesToTrack(grayFrames, points1, MAX_COUNT, 0.01, 5, cv::Mat(), 3, 0, 0.04);
        needToInit = false;
    } else if (!points2.empty()) {
        cv::calcOpticalFlowPyrLK(prevGrayFrame, grayFrames, points2, points1, status, err, winSize, 3, termcrit, 0, 0.001);

        int x_sum, y_sum = 0;

        int i, k;
        for (i = k = 0; i < points2.size(); i++) {
            int x = int(points1[i].x - points2[i].x);
            int y = int(points1[i].y - points2[i].y);

            x_sum = abs(x) + x_sum;
            y_sum = abs(y) + y_sum;

            if ((points1[i].x - points2[i].x) > 0) {
                cv::line(rgbFrames, points1[i], points2[i], cv::Scalar(0, 0, 255), 1, 1, 0);
                cv::circle(rgbFrames, points1[i], 2, cv::Scalar(255, 0, 0), 1, 1, 0);
                cv::line(opticalFlow, points1[i], points2[i], cv::Scalar(0, 0, 255), 1, 1, 0);
                cv::circle(opticalFlow, points1[i], 1, cv::Scalar(255, 0, 0), 1, 1, 0);
            } else {
                cv::line(rgbFrames, points1[i], points2[i], cv::Scalar(0, 255, 0), 1, 1, 0);
                cv::circle(rgbFrames, points1[i], 2, cv::Scalar(255, 0, 0), 1, 1, 0);
                cv::line(opticalFlow, points1[i], points2[i], cv::Scalar(0, 255, 0), 1, 1, 0);
                cv::circle(opticalFlow, points1[i], 1, cv::Scalar(255, 0, 0), 1, 1, 0);
            }
            points1[k++] = points1[i];
        }
        cv::goodFeaturesToTrack(grayFrames, points1, MAX_COUNT, 0.01, 10, cv::Mat(), 3, 0, 0.04);

        if(x_sum + y_sum > 778000){
            std_msgs::Float32 msgMovement;
            cout <<"Movement detected! :"<< (x_sum + y_sum) - 830000 << endl;
            msgMovement.data = (x_sum + y_sum) - 830000;
            movement_pub.publish(msgMovement);
        }
    }
}

cv::VideoWriter prepareVideoRecord(cv::VideoCapture cap){
    double dWidth = cap.get(CV_CAP_PROP_FRAME_WIDTH); //get the width of frames of the video
    double dHeight = cap.get(CV_CAP_PROP_FRAME_HEIGHT); //get the height of frames of the video

    cout << "Frame Size = " << dWidth << "x" << dHeight << endl;

    cv::Size frameSize(static_cast<int>(dWidth), static_cast<int>(dHeight));

    //Generate video file name based on the date
    std::stringstream ss;
    ss << "/home/ferran/.ros/visionLog/" << currentDateTime() << ".avi";
    std::string s = ss.str();

    cv::VideoWriter oVideoWriter (s, CV_FOURCC('D','I','V','X'), 8, frameSize, true); //initialize the VideoWriter object

    if ( !oVideoWriter.isOpened() ) //if not initialize the VideoWriter successfully, exit the program
    {
        cout << "ERROR: Failed to write the video" << endl;
    }

    return oVideoWriter;
}

/*This function creates an OpenCV circle per each dlib marker and
and attach them into the recording videoframe
*/
void shapeToPoints(cv::Mat &imgResult, full_object_detection shape){

    cv::Point2f currentPoint;
    for (unsigned int i = 0; i < 68; i++){
        currentPoint = cv::Point2f(shape.part(i)(0),shape.part(i)(1));
        cv::circle(imgResult, cvPoint(currentPoint.x,currentPoint.y),2,CV_RGB(255,0,0),-1,8,0);
    }
}

void stateActivityCallback(const std_msgs::String::ConstPtr& msg){
    state = msg->data.c_str();
}

void stopActivityCallback(const std_msgs::Empty::ConstPtr& msg){
    ros::shutdown();
    win.close_window();
}

void newChildCallback(const std_msgs::String::ConstPtr& msg){
    new_child = true;
}

//Make a class twoDtoThreeD points
/*void calibration(shape_predictor &pose_model, cv_image<bgr_pixel> &cimg, rectangle face, cv::Mat &rgbFrames){

    std::vector<cv::Point3f > modelPoints;
    modelPoints.push_back(cv::Point3f(-36.9522f,39.3518f,47.1217f));    //l eye
    modelPoints.push_back(cv::Point3f(35.446f,38.4345f,47.6468f));              //r eye
    modelPoints.push_back(cv::Point3f(-0.0697709f,18.6015f,87.9695f)); //nose
    modelPoints.push_back(cv::Point3f(-27.6439f,-29.6388f,73.8551f));   //l mouth
    modelPoints.push_back(cv::Point3f(28.7793f,-29.2935f,72.7329f));    //r mouth
    modelPoints.push_back(cv::Point3f(-87.2155f,15.5829f,-45.1352f));   //l ear
    modelPoints.push_back(cv::Point3f(85.8383f,14.9023f,-46.3169f));    //r ear

    op = cv::Mat(modelPoints);
    op = op / 35; //just a little normalization...
    rvec = cv::Mat(rv);
    double _d[9] = {1,0,0,
              0,-1,0,
             0,0,-1}; //rotation: looking at -z axis
    cv::Rodrigues(cv::Mat(3,3,CV_64FC1,_d),rvec);
    tv[0]=0;tv[1]=0;tv[2]=1;
    tvec = cv::Mat(tv);
    double _cm[9] = { 700,  0,      double(rgbFrames.size().width/2),
                      0,    700,    double(rgbFrames.size().height/2),
                      0,    0,      1 };  //"calibration matrix": center point at center of picture with 700 focal length.
    camMatrix = cv::Mat(3,3,CV_64FC1,_cm);

    std::vector<cv::Point2f > imagePoints;

    cv::Point2f eye_left = getPointFromPart(pose_model, cimg, face, "eyebrow_left");
    cv::Point2f eye_right = getPointFromPart(pose_model, cimg, face, "eyebrow_right");
    cv::Point2f nose = getPointFromPart(pose_model, cimg, face, "nose");
    cv::Point2f m_left = getPointFromPart(pose_model, cimg, face, "mouth_left");
    cv::Point2f m_right = getPointFromPart(pose_model, cimg, face, "mouth_right");
    cv::Point2f left= getPointFromPart(pose_model, cimg, face, "left_side");
    cv::Point2f right = getPointFromPart(pose_model, cimg, face, "right_side");

    imagePoints.push_back(eye_left);
    imagePoints.push_back(eye_right);
    imagePoints.push_back(nose);
    imagePoints.push_back(m_left);
    imagePoints.push_back(m_right);
    imagePoints.push_back(left);
    imagePoints.push_back(right);

    //make a Mat of the vector<>
    cv::Mat ip(imagePoints);

    //display points on image
    //cv::Mat img = imread("image.png");
    //for(unsigned int i=0;i<imagePoints.size();i++) circle(img,imagePoints[i],2,Scalar(255,0,255),CV_FILLED);

    //"distortion coefficients"... hah!
    double _dc[] = {0,0,0,0};

    //here's where the magic happens
    cv::solvePnP(op,ip,camMatrix,cv::Mat(1,4,CV_64FC1,_dc),rvec,tvec,true);

    //decompose the response to something OpenGL would understand.
    //translation vector is irrelevant, only rotation vector is important
    cv::Mat rotM(3,3,CV_64FC1);


    cv::Rodrigues(rvec,rotM);

    double* _r = rotM.ptr<double>();
    //printf("rotation mat: \n %.3f %.3f %.3f\n%.3f %.3f %.3f\n%.3f %.3f %.3f\n",
    //          _r[0],_r[1],_r[2],_r[3],_r[4],_r[5],_r[6],_r[7],_r[8]);

    cv::Mat Vx = (cv::Mat_<double>(3,1) << 1, 0, 0);
    cv::Mat Vy = (cv::Mat_<double>(3,1) << 0, 1, 0);
    cv::Mat Vz = (cv::Mat_<double>(3,1) << 0, 0, 1);

    cv::Mat x = rotM*Vx;
    cv::Mat y = rotM*Vy;
    cv::Mat z = rotM*Vz;
    double* _rx = x.ptr<double>();
    //printf("x mat: \n %.3f %.3f %.3f\n",
    //          _rx[0],_rx[1],_rx[2]);
    double* _ry = y.ptr<double>();
    //printf("y mat: \n %.3f %.3f %.3f\n",
    //          _ry[0],_ry[1],_ry[2]);
    double* _rz = z.ptr<double>();
    //printf("z mat: \n %.3f %.3f %.3f\n",
    //          _rz[0],_rz[1],_rz[2]);

    double mult =100;

    cv::Point2f nose_ax = nose + cv::Point2f(_rx[0],_rx[1])*mult;
    cv::Point2f nose_ay = nose + cv::Point2f(_ry[0],_ry[1])*mult;
    cv::Point2f nose_az = nose + cv::Point2f(_rz[0],_rz[1])*mult;

    cv::line(rgbFrames, nose, nose_ax, cv::Scalar(255, 0, 0), 8, 1, 0);
    cv::line(rgbFrames, nose, nose_ay, cv::Scalar(0, 255, 0), 8, 1, 0);
    cv::line(rgbFrames, nose, nose_az, cv::Scalar(0, 0, 255), 8, 1, 0);


    float focalLength = 700.0f;
    cv::Mat cameraMatrix = (cv::Mat_<float>(3,3) <<
        focalLength,    0,              rgbFrames.size().width/2,
        0,              focalLength,    rgbFrames.size().height/2,
        0,              0,              1
    );

    cv::Mat projectionMat = cv::Mat::zeros(4,4,CV_64FC1);
    cameraMatrix.copyTo(projectionMat(cv::Rect(0,0,3,3)));
    cv::Matx44f projection = projectionMat;
    projection(3,2) = 1;

    cv::Mat transformationMat = cv::Mat::zeros(4,4,CV_64FC1);
    rotM.copyTo(transformationMat(cv::Rect(0,0,3,3)));
    cv::Matx44f transformation = transformationMat;

    transformation(3,3)=1;

            static const float DEFAULT_SIZE = 20.f;
            static const cv::Vec4f UNITS[4] {
                {0.f, 0.f, 0.f, 1.f},
                {DEFAULT_SIZE, 0.f, 0.f, 1.f},
                {0.f, DEFAULT_SIZE, 0.f, 1.f},
                {0.f, 0.f, DEFAULT_SIZE, 1.f},
            };


            cv::Vec4f referential[4] = {
                projection*transformation*UNITS[0],
                projection*transformation*UNITS[1],
                projection*transformation*UNITS[2],
                projection*transformation*UNITS[3],
            };

            std::vector<cv::Point2f> t2DPoints;
            for (auto homogenousPoint : referential)
                t2DPoints.push_back(cv::Point2f(
                    homogenousPoint[0]/homogenousPoint[3],
                    homogenousPoint[1]/homogenousPoint[3]));

            static const int SHIFT = 2;
            static const float PRECISION = 1<<SHIFT;
            static const std::string AXIS_NAMES[3] = { "x", "y", "z" };
            static const cv::Scalar AXIS_COLORS[3] = {
                {0,0,255},{0,255,0},{255,0,0},
            };
            for (int i : {1,2,3}) {
                cv::line(
                    rgbFrames,
                    PRECISION*t2DPoints[0],
                    PRECISION*t2DPoints[i],
                    AXIS_COLORS[i-1],
                    1, CV_AA, SHIFT);

                cv::putText(rgbFrames, AXIS_NAMES[i-1], t2DPoints[i],
                            cv::FONT_HERSHEY_SIMPLEX, 0.5f, AXIS_COLORS[i-1]);
            }
}*/



int main(int argc, char **argv)
{

    /**
    * The ros::init() function needs to see argc and argv so that it can perform
    * any ROS arguments and name remapping that were provided at the command line.
    * The third argument to init() is the name of the node.
    *
    * You must call one of the versions of ros::init() before using any other
    * part of the ROS system.
    */
    ros::init(argc, argv, "vision");

    /**
     * NodeHandle is the main access point to communications with the ROS system.
     * The first NodeHandle constructed will fully initialize this node, and the last
     * NodeHandle destructed will close down the node.
     */
    ros::NodeHandle n;

    /**
     * The advertise() function is how you tell ROS that you want to
     * publish on a given topic name.The second parameter to advertise()
     * is the size of the message queue
     */
    ros::Publisher lookAt_pub = n.advertise<std_msgs::String>("lookAt", 1000);
    ros::Publisher smile_pub = n.advertise<std_msgs::Empty>("smile", 1000);
    ros::Publisher movement_pub = n.advertise<std_msgs::Int16>("movement", 1000);
    ros::Publisher sizeHead_pub = n.advertise<std_msgs::Int16>("sizeHead", 1000);
    ros::Publisher novelty_pub = n.advertise<std_msgs::Float32>("novelty", 1000);
    ros::Subscriber state_sub = n.subscribe("state_activity", 1000, stateActivityCallback);
    ros::Subscriber stop_sub = n.subscribe("stop_learning", 1000, stopActivityCallback);
    ros::Subscriber new_child_sub = n.subscribe("new_child", 1000, newChildCallback);

    ros::Rate loop_rate(20);

    try
    {
        ofstream myfile;
        auto filename = argv[1];
        myfile.open (filename);
        std::vector<full_object_detection> contacts;
        //"/home/ferran/Desktop/GT_ISG/4_Gabriel/2015-05-06_10:50:53.avi"
        cv::VideoCapture cap(0);
        cap.set(CV_CAP_PROP_FRAME_WIDTH, 640);
        cap.set(CV_CAP_PROP_FRAME_HEIGHT, 360);


        // Load face detection and pose estimation models.
        frontal_face_detector detector = get_frontal_face_detector();
        shape_predictor pose_model;
        deserialize("shape_predictor_68_face_landmarks.dat") >> pose_model;

        float threshold = 1;
        float mu = 0.1;
        float eps = 0.00000001;

        cv::Mat frame, grayFrames, rgbFrames, prevGrayFrame;
        cv::Mat opticalFlow = cv::Mat(cap.get(CV_CAP_PROP_FRAME_HEIGHT), cap.get(CV_CAP_PROP_FRAME_HEIGHT), CV_32FC3);

        std::vector<cv::Point2f> points1;
        std::vector<cv::Point2f> points2;
        bool needToInit = true;
        cv::VideoWriter oVideoWriter = prepareVideoRecord(cap);

        // Grab and process frames until the main window is closed by the user.
        while(!win.is_closed()) {
            cap >> frame;

            //Resize and crop the boundaries to get 4:3 proportion
            //cv::resize(frame, frame, cv::Size(640, 360), 0, 0, cv::INTER_CUBIC);
            //cv::Rect myROI(160, 0, 640, 480);
            //frame = frame(myROI);

            frame.copyTo(rgbFrames);
            cv::cvtColor(rgbFrames, grayFrames, CV_BGR2GRAY);

            // Amount of movement using optical flow
            if(state != "WAITING_FOR_FEEDBACK"){
                amountMovement(movement_pub, rgbFrames, grayFrames, prevGrayFrame, opticalFlow, points1, points2, needToInit);
            }else{
                needToInit = true;
            }

            if (new_child == true){
                oVideoWriter = prepareVideoRecord(cap);
                new_child = false;
            }

            oVideoWriter << frame;

            std::swap(points2, points1);
            points1.clear();
            grayFrames.copyTo(prevGrayFrame);


            /** Turn OpenCV's Mat into something dlib can deal with.  Note that this just wraps the Mat object,
             * it doesn't copy anything.  So cimg is only valid as long as temp is valid.
             */
            cv_image<bgr_pixel> cimg(rgbFrames);

            // Detect faces

            std::vector<rectangle> faces = detector(cimg);

            // Find the pose of each face.
            std::vector<full_object_detection> shapes;

            for (unsigned long i = 0; i < faces.size(); ++i){
                full_object_detection shape = pose_model(cimg, faces[i]);
                shapes.push_back(shape);
                //Convert to Point2f
                //shapeToPoints(rgbFrames, shape);

                std::vector<bool> lookTowards;
                lookTowards = lookAt(lookAt_pub, pose_model, cimg, faces[i], contacts, rgbFrames);
                sizeHead(sizeHead_pub, pose_model, cimg, faces[i]);
                smileDetector(smile_pub, pose_model, cimg, faces[i]);
                novelty(novelty_pub, lookTowards, faces, mu, eps, threshold);

                //3D pose  estimation
                //calibration(pose_model, cimg, faces[i], rgbFrames);
            }

            //Lets put the markers to the video
            //oVideoWriter.write(rgbFrames);
            //Simplify EMA value calulation and make a function
            if( faces.size() == 0){
                std::vector<float> X(6,0);
                auto temp = EMA;

                for (unsigned int j = 0; j < EMA.size(); ++j){
                    EMA[j] = mu*X[j] + (1-mu)*EMA[j];
                }

                float dist = 0.0;
                for (unsigned int j = 0; j < EMA.size(); ++j){
                    dist += 2*(EMA[j]-temp[j])*(EMA[j]-temp[j]) / ( eps+(EMA[j]+temp[j])*(EMA[j]+temp[j]) );
                }
                if( dist>threshold){
                    //cout <<"novelty ! :"<< dist*sqrt(t) << endl;
                    t = 1;
                }
                else{
                    t++;
                }
            }

            // Display it all on the screen
            win.clear_overlay();
            win.set_image(cimg);
            win.add_overlay(render_face_detections(shapes));
            ros::spinOnce();
        }
    }
    catch(serialization_error& e)
    {
        cout << "You need dlib's default face landmarking model file to run this example." << endl;
        cout << "You can get it from the following URL: " << endl;
        cout << "   http://sourceforge.net/projects/dclib/files/dlib/v18.10/shape_predictor_68_face_landmarks.dat.bz2" << endl;
        cout << endl << e.what() << endl;
    }
    catch(exception& e)
    {
        cout << e.what() << endl;
    }

    return 0;
}
