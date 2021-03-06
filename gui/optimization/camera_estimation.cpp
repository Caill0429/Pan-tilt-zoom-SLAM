//
//  camera_estimation.cpp
//  Annotation
//
//  Created by jimmy on 2019-01-27.
//  Copyright © 2019 Nowhere Planet. All rights reserved.
//

#include "camera_estimation.hpp"
#include <vpgl/algo/vpgl_calibration_matrix_compute.h>
#include <vpgl/algo/vpgl_camera_compute.h>
#include <vnl/vnl_least_squares_function.h>
#include <vnl/algo/vnl_levenberg_marquardt.h>
#include <vnl/vnl_inverse.h>
#include <vnl/algo/vnl_matrix_inverse.h>
#include <vgl/vgl_intersection.h>
#include <vgl/vgl_distance.h>
#include <vgl/algo/vgl_homg_operators_2d.h>

#include "bcv_vgl_h_matrix_2d_compute_linear.h"
#include "bcv_vgl_h_matrix_2d_optimize_lmq.h"
#include "bcv_vgl_h_matrix_2d_decompose.h"


namespace cvx {
	bool init_calib(const vector<vgl_point_2d<double> > &wld_pts,
		const vector<vgl_point_2d<double> > &img_pts,
		const vgl_point_2d<double> &principlePoint,
		vpgl_perspective_camera<double> &camera)
	{
		if (wld_pts.size() < 4 && img_pts.size() < 4) {
			return false;
		}
		if (wld_pts.size() != img_pts.size()) {
			return false;
		}
		assert(wld_pts.size() >= 4 && img_pts.size() >= 4);
		assert(wld_pts.size() == img_pts.size());

		vpgl_calibration_matrix<double> K;
		if (vpgl_calibration_matrix_compute::natural(img_pts, wld_pts, principlePoint, K) == false) {
			std::cerr << "Failed to compute K" << std::endl;
			std::cerr << "Default principle point: " << principlePoint << std::endl;
			return false;
		}

		camera.set_calibration(K);

		// vpgl_perspective_camera_compute_positiveZ
		if (vpgl_perspective_camera_compute::compute(img_pts, wld_pts, camera) == false) {
			std::cerr << "Failed to computer R, C" << std::endl;
			return false;
		}
		return true;
	}


	class optimize_perspective_camera_residual :public vnl_least_squares_function
	{
	protected:
		const std::vector<vgl_point_2d<double> > wldPts_;
		const std::vector<vgl_point_2d<double> > imgPts_;
		const vgl_point_2d<double> principlePoint_;

	public:
		optimize_perspective_camera_residual(const std::vector<vgl_point_2d<double> > & wldPts,
			const std::vector<vgl_point_2d<double> > & imgPts,
			const vgl_point_2d<double> & pp) :
			vnl_least_squares_function(7, (unsigned int)(wldPts.size()) * 2, no_gradient),
			wldPts_(wldPts),
			imgPts_(imgPts),
			principlePoint_(pp)
		{
			assert(wldPts.size() == imgPts.size());
			assert(wldPts.size() >= 4);
		}

		void f(vnl_vector<double> const &x, vnl_vector<double> &fx)
		{
			//focal length, Rxyz, Camera_center_xyz
			vpgl_calibration_matrix<double> K(x[0], principlePoint_);

			vnl_vector_fixed<double, 3> rod(x[1], x[2], x[3]);
			vgl_rotation_3d<double>  R(rod);
			vgl_point_3d<double> cc(x[4], x[5], x[6]);  //camera center

			vpgl_perspective_camera<double> camera;
			camera.set_calibration(K);
			camera.set_rotation(R);
			camera.set_camera_center(cc);

			//loop all points
			int idx = 0;
			for (int i = 0; i < wldPts_.size(); i++) {
				vgl_point_3d<double> p(wldPts_[i].x(), wldPts_[i].y(), 0);
				vgl_point_2d<double> proj_p = (vgl_point_2d<double>)camera.project(p);

				fx[idx] = imgPts_[i].x() - proj_p.x();
				idx++;
				fx[idx] = imgPts_[i].y() - proj_p.y();
				idx++;
			}
		}

		void getCamera(vnl_vector<double> const &x, vpgl_perspective_camera<double> &camera)
		{

			vpgl_calibration_matrix<double> K(x[0], principlePoint_);

			vnl_vector_fixed<double, 3> rod(x[1], x[2], x[3]);
			vgl_rotation_3d<double>  R(rod);
			vgl_point_3d<double> camera_center(x[4], x[5], x[6]);

			camera.set_calibration(K);
			camera.set_rotation(R);
			camera.set_camera_center(camera_center);
		}

	};


	bool optimize_perspective_camera(const std::vector<vgl_point_2d<double> > & wld_pts,
		const std::vector<vgl_point_2d<double> > & img_pts,
		const vpgl_perspective_camera<double> &init_camera,
		vpgl_perspective_camera<double> & final_camera)
	{
		assert(wld_pts.size() == img_pts.size());
		assert(wld_pts.size() >= 4);

		optimize_perspective_camera_residual residual(wld_pts, img_pts,
			init_camera.get_calibration().principal_point());

		vnl_vector<double> x(7, 0);
		x[0] = init_camera.get_calibration().get_matrix()[0][0];
		x[1] = init_camera.get_rotation().as_rodrigues()[0];
		x[2] = init_camera.get_rotation().as_rodrigues()[1];
		x[3] = init_camera.get_rotation().as_rodrigues()[2];
		x[4] = init_camera.camera_center().x();
		x[5] = init_camera.camera_center().y();
		x[6] = init_camera.camera_center().z();

		vnl_levenberg_marquardt lmq(residual);

		bool isMinimied = lmq.minimize(x);
		if (!isMinimied) {
			std::cerr << "Error: perspective camera optimize not converge.\n";
			lmq.diagnose_outcome();
			return false;
		}
		lmq.diagnose_outcome();

		//    lmq.diagnose_outcome();
		residual.getCamera(x, final_camera);
		return true;
	}


	bool init_calib(const vector<vgl_point_2d<double> >& world_pts,
		const vector<vgl_point_2d<double> >& image_pts,
		const vector<vgl_line_segment_2d<double>>& world_line_segment,
		const vector<vgl_line_segment_2d<double>>& image_line_segment,
		const vgl_point_2d<double> &principle_point,
		vpgl_perspective_camera<double> &camera)
	{
		assert(world_pts.size() == image_pts.size());
		assert(world_line_segment.size() == image_line_segment.size());

		// step 1: estimate H_world_to_image, First, image to world, then invert
		vector<vgl_homg_point_2d<double> > points1, points2;
		for (int i = 0; i < world_pts.size(); i++) {
			points1.push_back(vgl_homg_point_2d<double>(image_pts[i]));
			points2.push_back(vgl_homg_point_2d<double>(world_pts[i]));
		}

		vector<vgl_homg_line_2d<double> > lines1, lines2;
		vector<vector<vgl_homg_point_2d<double>> > point_on_line1;
		for (int i = 0; i < world_line_segment.size(); i++) {
			vgl_homg_point_2d<double> p1 = vgl_homg_point_2d<double>(image_line_segment[i].point1());
			vgl_homg_point_2d<double> p2 = vgl_homg_point_2d<double>(image_line_segment[i].point2());
			lines1.push_back(vgl_homg_line_2d<double>(p1, p2));

			vector<vgl_homg_point_2d<double>> pts;
			pts.push_back(p1);
			pts.push_back(p2);
			point_on_line1.push_back(pts);

			p1 = vgl_homg_point_2d<double>(world_line_segment[i].point1());
			p2 = vgl_homg_point_2d<double>(world_line_segment[i].point2());
			lines2.push_back(vgl_homg_line_2d<double>(p1, p2));
		}

		bool is_valid = false;
		bcv_vgl_h_matrix_2d_compute_linear hcl;
		vgl_h_matrix_2d<double> H;
		is_valid = hcl.compute_pl(points1, points2, lines1, lines2, H);
		if (!is_valid) {
			return false;
		}

		bcv_vgl_h_matrix_2d_optimize_lmq hcl_lmq(H);
		vgl_h_matrix_2d<double> opt_h;
		is_valid = hcl_lmq.optimize_pl(points1, points2, point_on_line1, lines2, opt_h);
		if (!is_valid) {
			return false;
		}

		// step 2: estimate K
		vpgl_calibration_matrix<double> K;
		is_valid = vpgl_calibration_matrix_compute::natural(opt_h.get_inverse(), principle_point, K);
		if (!is_valid) {
			return false;
		}


		vnl_matrix_fixed<double, 3, 3> h_world_to_img = opt_h.get_inverse().get_matrix();
		//cout<<"homography is "<<h_world_to_img<<endl;
		bcv_vgl_h_matrix_2d_decompose hd;
		std::vector<vnl_matrix_fixed<double, 3, 3>> rotations;
		std::vector<vnl_vector_fixed<double, 3>> translations;
		is_valid = hd.compute(K.get_matrix(), h_world_to_img.as_matrix(), rotations, translations);
		if (!is_valid) {
			return false;
		}

		assert(rotations.size() == translations.size());
		assert(rotations.size() == 2);

		vnl_matrix<double> invR1 = vnl_matrix_inverse<double>(rotations[0].as_matrix());
		vnl_vector<double> cc1 = -invR1*translations[0];

		vnl_matrix<double> invR2 = vnl_matrix_inverse<double>(rotations[1].as_matrix());
		vnl_vector<double> cc2 = -invR2*translations[1];

		if (cc1[2] < 0 && cc2[2] < 0) {
			printf("Warning: two solutions are below z = 0 plane \n");
			return false;
		}
		else if (cc1[2] >= 0 && cc2[2] >= 0) {
			printf("Warning: two ambiguity solutions \n");
			return false;
		}

		vnl_matrix_fixed<double, 3, 3> R = cc1[2] > 0 ? rotations[0] : rotations[1];
		vnl_vector<double> cc = cc1[2] > 0 ? cc1 : cc2;

		camera.set_calibration(K);
		camera.set_rotation(vgl_rotation_3d<double>(R));
		camera.set_camera_center(vgl_point_3d<double>(cc[0], cc[1], cc[2]));

		return true;
	}


	class optimize_perspective_camera_ICP_residual : public vnl_least_squares_function
	{
	protected:
		const vector<vgl_point_2d<double> > wldPts_;
		const vector<vgl_point_2d<double> > imgPts_;
		const vector<vgl_line_3d_2_points<double> >  wldLines_;
		const vector<vector<vgl_point_2d<double> > >  imgLinePts_;
		const vgl_point_2d<double> principlePoint_;
	public:
		optimize_perspective_camera_ICP_residual(const vector<vgl_point_2d<double> > & wldPts,
			const vector<vgl_point_2d<double> > & imgPts,
			const vector<vgl_line_3d_2_points<double> >  & wldLines,
			const vector<vector<vgl_point_2d<double> > >  & imgLinePts,
			const vgl_point_2d<double> & pp,
			const int num_line_pts) :
			vnl_least_squares_function(7, (unsigned int)(wldPts.size()) * 2 + num_line_pts, no_gradient),
			wldPts_(wldPts),
			imgPts_(imgPts),
			wldLines_(wldLines),
			imgLinePts_(imgLinePts),
			principlePoint_(pp)
		{
			assert(wldPts.size() == imgPts.size());
			assert(wldPts.size() + wldLines.size() >= 4);
			assert(wldLines.size() == imgLinePts.size());
		}

		void f(vnl_vector<double> const &x, vnl_vector<double> &fx)
		{
			//focal length, Rxyz, Camera_center_xyz
			vpgl_calibration_matrix<double> K(x[0], principlePoint_);

			vnl_vector_fixed<double, 3> rod(x[1], x[2], x[3]);
			vgl_rotation_3d<double>  R(rod);
			vgl_point_3d<double> cc(x[4], x[5], x[6]);  //camera center

			vpgl_perspective_camera<double> camera;
			camera.set_calibration(K);
			camera.set_rotation(R);
			camera.set_camera_center(cc);

			//loop all points
			int idx = 0;
			for (int i = 0; i < wldPts_.size(); i++) {
				vgl_point_3d<double> p(wldPts_[i].x(), wldPts_[i].y(), 0);
				vgl_point_2d<double> proj_p = (vgl_point_2d<double>)camera.project(p);

				fx[idx] = imgPts_[i].x() - proj_p.x();
				idx++;
				fx[idx] = imgPts_[i].y() - proj_p.y();
				idx++;
			}

			// for points locate on the line
			for (int i = 0; i < wldLines_.size(); i++) {
				vgl_point_2d<double> p1 = camera.project(wldLines_[i].point1());
				vgl_point_2d<double> p2 = camera.project(wldLines_[i].point2());
				vgl_line_2d<double> line(p1, p2);
				for (int j = 0; j < imgLinePts_[i].size(); j++) {
					vgl_point_2d<double> p3 = imgLinePts_[i][j];
					fx[idx] = vgl_distance(line, p3);
					idx++;
				}
			}
		}

		void getCamera(vnl_vector<double> const &x, vpgl_perspective_camera<double> &camera)
		{

			vpgl_calibration_matrix<double> K(x[0], principlePoint_);

			vnl_vector_fixed<double, 3> rod(x[1], x[2], x[3]);
			vgl_rotation_3d<double>  R(rod);
			vgl_point_3d<double> camera_center(x[4], x[5], x[6]);

			camera.set_calibration(K);
			camera.set_rotation(R);
			camera.set_camera_center(camera_center);
		}
	};


	bool optimize_perspective_camera_ICP(const vector<vgl_point_2d<double> > &wld_pts,
		const vector<vgl_point_2d<double> > &img_pts,
		const vector<vgl_line_3d_2_points<double> > & wld_lines,
		const vector<vector<vgl_point_2d<double> > > & img_line_pts,
		const vpgl_perspective_camera<double> & init_camera,
		vpgl_perspective_camera<double> &camera)
	{
		assert(wld_pts.size() == img_pts.size());
		assert(wld_pts.size() + wld_lines.size() >= 4);
		assert(wld_lines.size() == img_line_pts.size());

		int num_line_pts = 0;
		for (int i = 0; i < img_line_pts.size(); i++) {
			num_line_pts += (int)img_line_pts[i].size();
		}
		optimize_perspective_camera_ICP_residual residual(wld_pts, img_pts, wld_lines, img_line_pts, init_camera.get_calibration().principal_point(), num_line_pts);

		vnl_vector<double> x(7, 0);
		x[0] = init_camera.get_calibration().get_matrix()[0][0];
		x[1] = init_camera.get_rotation().as_rodrigues()[0];
		x[2] = init_camera.get_rotation().as_rodrigues()[1];
		x[3] = init_camera.get_rotation().as_rodrigues()[2];
		x[4] = init_camera.camera_center().x();
		x[5] = init_camera.camera_center().y();
		x[6] = init_camera.camera_center().z();

		vnl_levenberg_marquardt lmq(residual);

		bool isMinimied = lmq.minimize(x);
		if (!isMinimied) {
			std::cerr << "Error: perspective camera optimize not converge.\n";
			lmq.diagnose_outcome();
			return false;
		}
		lmq.diagnose_outcome();
		residual.getCamera(x, camera);
		return true;
	}

	vnl_matrix_fixed<double, 3, 3> homography_from_projective_camera(const vpgl_perspective_camera<double> & camera)
	{
		vnl_matrix_fixed<double, 3, 3> H;
		vnl_matrix_fixed<double, 3, 4> P = camera.get_matrix();

		H(0, 0) = P(0, 0); H(0, 1) = P(0, 1); H(0, 2) = P(0, 3);
		H(1, 0) = P(1, 0); H(1, 1) = P(1, 1); H(1, 2) = P(1, 3);
		H(2, 0) = P(2, 0); H(2, 1) = P(2, 1); H(2, 2) = P(2, 3);

		return H;
	}

	vgl_conic<double> project_conic(const vnl_matrix_fixed<double, 3, 3> & H, const vgl_conic<double> & conic)
	{
		double a = conic.a();
		double b = conic.b();
		double c = conic.c();
		double d = conic.d();
		double e = conic.e();
		double f = conic.f();
		vnl_matrix_fixed<double, 3, 3> C;
		C(0, 0) = a;     C(0, 1) = b / 2.0; C(0, 2) = d / 2.0;
		C(1, 0) = b / 2.0; C(1, 1) = c;     C(1, 2) = e / 2.0;
		C(2, 0) = d / 2.0; C(2, 1) = e / 2.0; C(2, 2) = f;
		// project conic by H
		vnl_matrix_fixed<double, 3, 3> H_inv = vnl_inverse(H);
		vnl_matrix_fixed<double, 3, 3> C_proj = H_inv.transpose() * C * H_inv;

		// approximate a conic from 3*3 matrix
		double aa = C_proj(0, 0);
		double bb = (C_proj(0, 1) + C_proj(1, 0));
		double cc = C_proj(1, 1);
		double dd = (C_proj(0, 2) + C_proj(2, 0));
		double ee = (C_proj(1, 2) + C_proj(2, 1));
		double ff = C_proj(2, 2);

		// project conic
		vgl_conic<double> conic_proj(aa, bb, cc, dd, ee, ff);
		return conic_proj;
	}

	class optimize_perspective_camera_line_conic_ICP_residual : public vnl_least_squares_function
	{
	protected:
		const vector<vgl_point_2d<double> > wldPts_;
		const vector<vgl_point_2d<double> > imgPts_;
		const vector<vgl_line_3d_2_points<double> >  wldLines_;
		const vector<vector<vgl_point_2d<double> > >  imgLinePts_;
		const vector<vgl_conic<double> >  wldConics_;
		const vector<vector<vgl_point_2d<double> > >  imgConicPts_;
		const vgl_point_2d<double> principlePoint_;
	public:
		optimize_perspective_camera_line_conic_ICP_residual(const vector<vgl_point_2d<double> > & wldPts,
			const vector<vgl_point_2d<double> > & imgPts,
			const vector<vgl_line_3d_2_points<double> > & wldLines,
			const vector<vector<vgl_point_2d<double> > > & imgLinePts,

			const vector<vgl_conic<double> >  & wldConics,
			const vector<vector<vgl_point_2d<double> > >  & imgConicPts,

			const vgl_point_2d<double> & pp,
			const int num_line_pts,
			const int num_conic_pts) :
			vnl_least_squares_function(7, (unsigned int)(wldPts.size()) * 2 + num_line_pts + num_conic_pts, no_gradient),
			wldPts_(wldPts),
			imgPts_(imgPts),
			wldLines_(wldLines),
			imgLinePts_(imgLinePts),
			wldConics_(wldConics),
			imgConicPts_(imgConicPts),
			principlePoint_(pp)
		{
			assert(wldPts.size() == imgPts.size());
			assert(wldPts.size() >= 2);
			assert(wldLines.size() == imgLinePts.size());
			assert(wldConics.size() == imgConicPts.size());
		}

		void f(vnl_vector<double> const &x, vnl_vector<double> &fx)
		{
			//focal length, Rxyz, Camera_center_xyz
			vpgl_calibration_matrix<double> K(x[0], principlePoint_);
			vnl_vector_fixed<double, 3> rod(x[1], x[2], x[3]);
			vgl_rotation_3d<double>  R(rod);
			vgl_point_3d<double> cc(x[4], x[5], x[6]);  //camera center
			vpgl_perspective_camera<double> camera;
			camera.set_calibration(K);
			camera.set_rotation(R);
			camera.set_camera_center(cc);

			//loop all points
			int idx = 0;
			for (int i = 0; i < wldPts_.size(); i++) {
				vgl_point_3d<double> p(wldPts_[i].x(), wldPts_[i].y(), 0);
				vgl_point_2d<double> proj_p = (vgl_point_2d<double>)camera.project(p);

				fx[idx] = imgPts_[i].x() - proj_p.x();
				idx++;
				fx[idx] = imgPts_[i].y() - proj_p.y();
				idx++;
			}

			// for points locate on the line
			for (int i = 0; i < wldLines_.size(); i++) {
				vgl_point_2d<double> p1 = camera.project(wldLines_[i].point1());
				vgl_point_2d<double> p2 = camera.project(wldLines_[i].point2());
				vgl_line_2d<double> line(p1, p2);
				for (int j = 0; j < imgLinePts_[i].size(); j++) {
					vgl_point_2d<double> p3 = imgLinePts_[i][j];
					fx[idx] = vgl_distance(line, p3);
					idx++;
				}
			}

			// for points locate on the conics
			vnl_matrix_fixed<double, 3, 3> H = homography_from_projective_camera(camera);
			for (int i = 0; i < wldConics_.size(); i++) {
				vgl_conic<double> conic_proj = project_conic(H, wldConics_[i]);
				for (int j = 0; j < imgConicPts_[i].size(); j++) {
					vgl_point_2d<double> p = imgConicPts_[i][j];
					double dis = vgl_homg_operators_2d<double>::distance_squared(conic_proj, vgl_homg_point_2d<double>(p.x(), p.y(), 1.0));
					dis = sqrt(dis + 0.0000001);
					fx[idx] = dis;
					idx++;
				}
			}

		}

		void getCamera(vnl_vector<double> const &x, vpgl_perspective_camera<double> &camera)
		{

			vpgl_calibration_matrix<double> K(x[0], principlePoint_);

			vnl_vector_fixed<double, 3> rod(x[1], x[2], x[3]);
			vgl_rotation_3d<double>  R(rod);
			vgl_point_3d<double> camera_center(x[4], x[5], x[6]);

			camera.set_calibration(K);
			camera.set_rotation(R);
			camera.set_camera_center(camera_center);
		}
	};



	bool optimize_perspective_camera_ICP(const vector<vgl_point_2d<double> > &wldPts,
		const vector<vgl_point_2d<double> > &imgPts,
		const vector<vgl_line_3d_2_points<double> > & wldLines,
		const vector<vector<vgl_point_2d<double> > > & imgLinePts,
		const vector<vgl_conic<double> > & wldConics,
		const vector<vector<vgl_point_2d<double> > > & imgConicPts,
		const vpgl_perspective_camera<double> & initCamera,
		vpgl_perspective_camera<double> &camera)
	{
		assert(wldPts.size() == imgPts.size());
		assert(wldLines.size() == imgLinePts.size());
		assert(wldConics.size() == imgConicPts.size());

		int num_line_pts = 0;
		int num_conic_pts = 0;
		for (int i = 0; i < imgLinePts.size(); i++) {
			num_line_pts += imgLinePts[i].size();
		}
		for (int i = 0; i < imgConicPts.size(); i++) {
			num_conic_pts += imgConicPts[i].size();
		}
		assert((unsigned int)(wldPts.size()) * 2 + num_line_pts + num_conic_pts > 7);

		optimize_perspective_camera_line_conic_ICP_residual residual(wldPts, imgPts, wldLines, imgLinePts, wldConics, imgConicPts,
			initCamera.get_calibration().principal_point(),
			num_line_pts, num_conic_pts);
		vnl_vector<double> x(7, 0);
		x[0] = initCamera.get_calibration().get_matrix()[0][0];
		x[1] = initCamera.get_rotation().as_rodrigues()[0];
		x[2] = initCamera.get_rotation().as_rodrigues()[1];
		x[3] = initCamera.get_rotation().as_rodrigues()[2];
		x[4] = initCamera.camera_center().x();
		x[5] = initCamera.camera_center().y();
		x[6] = initCamera.camera_center().z();

		vnl_levenberg_marquardt lmq(residual);
		bool isMinimied = lmq.minimize(x);
		if (!isMinimied) {
			printf("Error: perspective camera optimize not converge.\n");
			lmq.diagnose_outcome();
			return false;
		}
		lmq.diagnose_outcome();
		residual.getCamera(x, camera);
		return true;
	}

	bool optimize_perspective_camera_point_line_circle(const vector<vgl_point_2d<double> > &wldPts,
		const vector<vgl_point_2d<double> > &imgPts,
		const vector<vgl_line_3d_2_points<double> > & wldLines,
		const vector<vector<vgl_point_2d<double> > > & imgLinePts,
		const vector<vgl_conic<double> > & wldConics,
		const vector<vgl_point_2d<double>> & imgConicPts,
		const vpgl_perspective_camera<double> & initCamera,
		vpgl_perspective_camera<double> &camera)
	{
		// for points locate on the conics
		vnl_matrix_fixed<double, 3, 3> H = homography_from_projective_camera(initCamera);
		vector<vgl_conic<double>> projected_conics;
		for (int i = 0; i < wldConics.size(); i++) {
			projected_conics.push_back(project_conic(H, wldConics[i]));
		}

		vector<vector<vgl_point_2d<double>> > img_conic_pts_groups(wldConics.size());
		// min projection error
		for (const vgl_point_2d<double>& p : imgConicPts) {
			int min_index = -1;
			double min_dist = INT_MAX;
			for (int j = 0; j < projected_conics.size(); j++) {
				vgl_conic<double> conic_proj = projected_conics[j];
				double dist = vgl_homg_operators_2d<double>::distance_squared(conic_proj, vgl_homg_point_2d<double>(p.x(), p.y(), 1.0));
				if (dist < min_dist) {
					min_dist = dist;
					min_index = j;
				}
			}
			if (min_index != -1) {
				img_conic_pts_groups[min_index].push_back(p);
			}
		}

		return optimize_perspective_camera_ICP(wldPts, imgPts,
			wldLines, imgLinePts,
			wldConics, img_conic_pts_groups,
			initCamera, camera);
	}

}
