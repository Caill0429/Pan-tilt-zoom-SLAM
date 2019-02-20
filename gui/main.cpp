#include <iostream>
#include <string>

#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/calib3d/calib3d.hpp>

#include <vpgl/vpgl_perspective_camera.h>
#include <vgl/vgl_point_2d.h>

#include "annotation_window.h"
#include "io/io_util.hpp"

using std::string;

int main(int argc, char *argv[])
{
#ifdef _WIN32
    string im_name = string("C:/graduate_design/cvui/cvuiApp/cvuiApp/1.jpg");
    string model_name = string("C:/graduate_design/cvui/cvuiApp/cvuiApp/model.png");
#elif __APPLE__
    string im_name("/Users/jimmy/Code/ptz_slam/Pan-tilt-zoom-SLAM/gui/1.jpg");
    string model_name("/Users/jimmy/Code/ptz_slam/Pan-tilt-zoom-SLAM/gui/model.png");
#endif

	std::vector<vgl_point_2d<double>> points;
	std::vector<std::pair<int, int>> pairs;
	io_util::readModel("./resource/ice_hockey_model.txt", points, pairs);


	AnnotationWindow app("Main View");

	FeatureAnnotationView * feature_annotation = new FeatureAnnotationView("Feature Annotation");
	CourtView * court_view = new CourtView("Court View");
	

	court_view->setWindowSize(cv::Size(800, 400));
	court_view->setImagePosition(cv::Point(19, 26));
	court_view->setImageSize(cv::Size(762, 348));

	cv::Mat source_img = cv::imread(im_name, cv::IMREAD_COLOR);
	cv::Mat model_img = cv::imread(model_name, cv::IMREAD_COLOR);

	feature_annotation->setImage(source_img);
	court_view->setImage(model_img);

	app.setFeatureAnnotationView(feature_annotation);
	app.setCourtView(court_view);
	

	app.startLoop();

	return 0;
}
