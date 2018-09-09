#include <exception>
#include "ofxRSSDKv2.h"
#include "ofMain.h"

//sdjk https://github.com/IntelRealSense/librealsense
//extrinsic data: https://github.com/IntelRealSense/librealsense/blob/5e73f7bb906a3cbec8ae43e888f182cc56c18692/examples/sensor-control/api_how_to.h#L209
// projection: https://github.com/IntelRealSense/librealsense/wiki/Projection-in-RealSense-SDK-2.0
// howtos: https://github.com/IntelRealSense/librealsense/wiki/API-How-To#get-depth-units

namespace ofxRSSDK
{	
	Disparity::~Disparity(){}

	Disparity::Disparity(){
		rs2::disparity_transform depth_to_disp(true);
		rs2::disparity_transform disp_to_depth(false);
		filter_in = depth_to_disp;
		filter_out = disp_to_depth;
	}

	RSDevice::~RSDevice(){}

	RSDevice::RSDevice(){
		mIsInit = false;
		mIsRunning = false;
		mHasRgb = false;
		mHasDepth = false;
		mShouldAlign = false;
		mShouldGetDepthAsColor = false;
		mShouldGetPointCloud = false;
		mPointCloudRange = ofVec2f(0,3000);
		mCloudRes = CloudRes::FULL_RES;
		isUsingFilterDec = false;
		isUsingFilterSpat = false;
		isUsingFilterTemp = false;
		isUsingFilterDisparity = false;
	}

#pragma region Init

#pragma endregion

	void RSDevice::setPointCloudRange(float pMin=100.0f, float pMax=1500.0f)
	{
		mPointCloudRange = ofVec2f(pMin,pMax);
	}

	void RSDevice::useFilterDecimation(int magnitude) {
		rs2Filter_dec.set_option(RS2_OPTION_FILTER_MAGNITUDE, magnitude);
		isUsingFilterDec = true;
	}

	void RSDevice::useFilterSpatialEdgePreserve(int magnitude, float smoothAlpha, int smoothDelta, int holeFilling) {
		rs2Filter_spat.set_option(RS2_OPTION_FILTER_MAGNITUDE, magnitude);
		rs2Filter_spat.set_option(RS2_OPTION_FILTER_SMOOTH_ALPHA, smoothAlpha);
		rs2Filter_spat.set_option(RS2_OPTION_FILTER_SMOOTH_DELTA , smoothDelta);
		rs2Filter_spat.set_option(RS2_OPTION_HOLES_FILL, holeFilling);
		isUsingFilterSpat = true;
	}

	void RSDevice::useFilterTemporal(float smoothAlpha, int smoothDelta, int persitency) {
		rs2Filter_temp.set_option(RS2_OPTION_FILTER_SMOOTH_ALPHA, smoothAlpha);
		rs2Filter_temp.set_option(RS2_OPTION_FILTER_SMOOTH_DELTA, smoothDelta);
		rs2Filter_temp.set_option(RS2_OPTION_HOLES_FILL, persitency);
		isUsingFilterTemp = true;
	}

	void RSDevice::useFilterDisparity() {
		isUsingFilterDisparity = true;
	}

	bool RSDevice::start()
	{
		mPointCloud.clear();
		mPointCloud.setMode(OF_PRIMITIVE_POINTS);
		mPointCloud.enableColors();

		//Create a configuration for configuring the pipeline with a non default profile
		//rs2::config cfg;

		//Add desired streams to configuration
		//cfg.enable_stream(RS2_STREAM_DEPTH, RS2_FORMAT_Z16); // Enable default depth
											 // For the color stream, set format to RGBA
											 // To allow blending of the color frame on top of the depth frame
		//cfg.enable_stream(RS2_STREAM_COLOR, RS2_FORMAT_RGB8);
		//cfg.enable_stream(RS2_STREAM_DEPTH, 1280, 720, RS2_FORMAT_Z16, 30);
		//cfg.enable_stream(RS2_STREAM_COLOR, 640, 480, RS2_FORMAT_RGB8, 30);
		
		rs2PipeLineProfile = rs2Pipe.start();
		
		// Declare filters
		rs2::decimation_filter dec_filter;
		rs2::spatial_filter spat_filter;

		// Configure filter parameters
		dec_filter.set_option(RS2_OPTION_FILTER_MAGNITUDE, 3.);
		spat_filter.set_option(RS2_OPTION_FILTER_SMOOTH_ALPHA, 0.55f);

		auto depth_stream = rs2PipeLineProfile.get_stream(RS2_STREAM_DEPTH).as<rs2::video_stream_profile>();
		auto color_sream = rs2PipeLineProfile.get_stream(RS2_STREAM_COLOR).as<rs2::video_stream_profile>();

		mRgbSize.x = color_sream.width();
		mRgbSize.y = color_sream.height();

		if (mShouldAlign)
		{
			mColorToDepthFrame.allocate(mRgbSize.x, mRgbSize.y, ofPixelFormat::OF_PIXELS_RGBA);
			mDepthToColorFrame.allocate(mRgbSize.x, mRgbSize.y, ofPixelFormat::OF_PIXELS_RGBA);
		}

		// Capture 30 frames to give autoexposure, etc. a chance to settle
		for (auto i = 0; i < 30; ++i) rs2Pipe.wait_for_frames();

		mIsRunning = true;
		mHasChangedResolution = true;

		return true;
	}

	bool RSDevice::isRunning() {
		return mIsRunning;
	}

	bool RSDevice::update()
	{
		if (rs2Pipe.poll_for_frames(&rs2FrameSet))
		{
			rs2Depth = rs2FrameSet.first(RS2_STREAM_DEPTH);
	
			if (rs2Depth)
			{
				if (isUsingFilterDec) {
					rs2Depth = rs2Filter_dec.process(rs2Depth);
				}
				if (isUsingFilterDisparity) {
					rs2Depth = rs2Filter_disparity.filter_in.process(rs2Depth);
				}
				if (isUsingFilterSpat) {
					rs2Depth = rs2Filter_spat.process(rs2Depth);
				}
				if (isUsingFilterTemp) {
					rs2Depth = rs2Filter_temp.process(rs2Depth);
				}
				if (isUsingFilterDisparity) {
					rs2Depth = rs2Filter_disparity.filter_out.process(rs2Depth);
				}

				// Use the colorizer to get an rgb image for the depth stream
				auto rs2DepthVideoFrame = rs2Color_map(rs2Depth);

				mDepthSize.x = rs2DepthVideoFrame.get_width();
				mDepthSize.y = rs2DepthVideoFrame.get_height();

				mDepthFrame.setFromExternalPixels((unsigned char *)rs2DepthVideoFrame.get_data(), mDepthSize.x, mDepthSize.y, 3);
			}

			auto rs2VideoFrame = rs2FrameSet.first(RS2_STREAM_COLOR).as<rs2::video_frame>();

			if (rs2VideoFrame)
			{
				mRgbFrame.setFromExternalPixels((unsigned char *)rs2VideoFrame.get_data(), rs2VideoFrame.get_width(), rs2VideoFrame.get_height(), 3);
			}
			return true;
		}
	
		return false;
	}

	bool RSDevice::stop()
	{
		rs2Pipe.stop();
		mIsRunning = false;
		return true;
		
	}

#pragma region Enable

#pragma endregion

#pragma region Update
	void RSDevice::updatePointCloud()
	{
		if (mRgbFrame.size()) {
			updatePointCloud(mRgbFrame);
		}
		else {
			updatePointCloud(mDepthFrame);
		}
	}

	void RSDevice::updatePointCloud(ofPixels colors)
	{
		// Generate the pointcloud and texture mapping	s
		rs2Points = rs2PointCloud.calculate(rs2Depth);

		int dWidth = (int)mDepthSize.x;
		int dHeight = (int)mDepthSize.y;
		int cWidth = colors.getWidth();
		int cHeight = colors.getHeight();

		int step = (int)mCloudRes;

		int length = dHeight * dWidth / (step * step);

		if (length != mPointCloud.getVertices().size()) {
			mPointCloud.clear();
			for (int i = 0; i < length; i++) {
				mPointCloud.addVertex(glm::vec3(0, 0, 0));
				mPointCloud.addColor(ofDefaultColorType());
			}
			cout << "created new depth point cloud w: " << dWidth << " h: " << dHeight << endl;
			//cout << "created new mesh: " << dHeight << "/" << dWidth << endl;
		}
		
		//float firstTime = ofGetElapsedTimef();  

		auto vertices = rs2Points.get_vertices();              // get vertices

		glm::vec3* pVertices = mPointCloud.getVerticesPointer();
		ofDefaultColorType* pColors = mPointCloud.getColorsPointer();

		int i_dOrig, i_dTarget;
		float relHeight = (float)cHeight / (float)dHeight;
		float relWidth = (float)cWidth / (float)dWidth;

		//cout << "relHeight: " << relHeight << " relWidth: " << relWidth << endl;

		for (int dy = 0; dy < dHeight; dy+=step)
		{
			int cy = dy * relHeight;
			auto pxlLine = colors.getLine(cy);

			for (int dx = 0; dx < dWidth; dx+=step)
			{
				int cx = dx * relHeight;
				auto pxl = pxlLine.getPixel(cx);

				i_dOrig = dy * dWidth + dx;

				i_dTarget = dy * dWidth / (step * step) + dx / step;

				pVertices[i_dTarget].x = vertices[i_dOrig].x;
				pVertices[i_dTarget].y = vertices[i_dOrig].y;
				pVertices[i_dTarget].z = vertices[i_dOrig].z;
				
				pColors[i_dTarget].r = pxl[0] / 255.;
				pColors[i_dTarget].g = pxl[1] / 255.;
				pColors[i_dTarget].b = pxl[2] / 255.;
			}
		}

		/*
		float lastTime = ofGetElapsedTimef();
		cout << "final depth point cloud size: " << mPointCloud.getVertices().size() << endl;
		cout << "elapsed time " << lastTime - firstTime  << endl;
		*/

	}

	bool RSDevice::draw()
	{
		mPointCloud.setMode(OF_PRIMITIVE_POINTS);
		mPointCloud.enableColors();
		mPointCloud.draw();
		return true;
	}

	bool RSDevice::drawColor(const ofRectangle & rect)
	{
		if (mRgbFrame.getWidth() > 0) {
			ofTexture texRGB;
			texRGB.loadData(mRgbFrame);
			texRGB.draw(rect.x, rect.y, rect.width, rect.height);
			return true;
		}
		return false;
	}

	bool RSDevice::drawDepth(const ofRectangle & rect)
	{
		if (mDepthFrame.getWidth() > 0) {
			ofTexture texRGB;
			texRGB.loadData(mDepthFrame);
			texRGB.draw(rect.x, rect.y, rect.width, rect.height);
			return true;
		}
		return false;
	}

#pragma endregion

#pragma region Getters
	const ofPixels& RSDevice::getRgbFrame()
	{
		return mRgbFrame;
	}

	const ofPixels& RSDevice::getDepthFrame()
	{
		return mDepthFrame;
	}

	const ofPixels& RSDevice::getColorMappedToDepthFrame()
	{
		return mColorToDepthFrame;
	}

	const ofPixels& RSDevice::getDepthMappedToColorFrame()
	{
		return mDepthToColorFrame;
	}

	ofMesh RSDevice::getPointCloud()
	{
		return mPointCloud;
	}

	vector<glm::vec3> & RSDevice::getPointCloudVertices()
	{
		return mPointCloud.getVertices();
	}

	//Nomenclature Notes:
	//	"Space" denotes a 3d coordinate
	//	"Image" denotes an image space point ((0, width), (0,height), (image depth))
	//	"Coords" denotes texture space (U,V) coordinates
	//  "Frame" denotes a full Surface

	//get a camera space point from a depth image point
	const ofPoint RSDevice::getDepthSpacePoint(float pImageX, float pImageY, float pImageZ)
	{
		/**
		if (mCoordinateMapper)
		{
			PXCPoint3DF32 cPoint;
			cPoint.x = pImageX;
			cPoint.y = pImageY;
			cPoint.z = pImageZ;

			mInPoints3D.clear();
			mInPoints3D.push_back(cPoint);
			mOutPoints3D.clear();
			mOutPoints3D.resize(2);
			mCoordinateMapper->ProjectDepthToCamera(1, &mInPoints3D[0], &mOutPoints3D[0]);
			return ofPoint(mOutPoints3D[0].x, mOutPoints3D[0].y, mOutPoints3D[0].z);
		}
		**/
		return ofPoint(0);
	}

	const ofPoint RSDevice::getDepthSpacePoint(int pImageX, int pImageY, uint16_t pImageZ)
	{
		return getDepthSpacePoint(static_cast<float>(pImageX), static_cast<float>(pImageY), static_cast<float>(pImageZ));
	}

	const ofPoint RSDevice::getDepthSpacePoint(ofPoint pImageCoords)
	{
		return getDepthSpacePoint(pImageCoords.x, pImageCoords.y, pImageCoords.z);
	}

	//get a Color object from a depth image point
	const ofColor RSDevice::getColorFromDepthImage(float pImageX, float pImageY, float pImageZ)
	{
		/**
		if (mCoordinateMapper)
		{
			PXCPoint3DF32 cPoint;
			cPoint.x = pImageX;
			cPoint.y = pImageY;
			cPoint.z = pImageZ;
			PXCPoint3DF32 *cInPoint = new PXCPoint3DF32[1];
			cInPoint[0] = cPoint;
			PXCPointF32 *cOutPoints = new PXCPointF32[1];
			mCoordinateMapper->MapDepthToColor(1, cInPoint, cOutPoints);

			float cColorX = cOutPoints[0].x;
			float cColorY = cOutPoints[0].y;

			delete cInPoint;
			delete cOutPoints;
			if (cColorX >= 0 && cColorX < mRgbSize.x&&cColorY >= 0 && cColorY < mRgbSize.y)
			{
				return mRgbFrame.getColor(cColorX, cColorY);
			}
		}
		**/
		return ofColor::black;
	}

	const ofColor RSDevice::getColorFromDepthImage(int pImageX, int pImageY, uint16_t pImageZ)
	{
		/**
		if (mCoordinateMapper)
			return getColorFromDepthImage(static_cast<float>(pImageX),static_cast<float>(pImageY),static_cast<float>(pImageZ));
		**/
		return ofColor::black;
	}

	const ofColor RSDevice::getColorFromDepthImage(ofPoint pImageCoords)
	{
		/**
		if (mCoordinateMapper)
			return getColorFromDepthImage(pImageCoords.x, pImageCoords.y, pImageCoords.z);
			**/
		return ofColor::black;
	}


		//get a ofColor object from a depth camera space point
	const ofColor RSDevice::getColorFromDepthSpace(float pCameraX, float pCameraY, float pCameraZ)
	{
			return ofColor::black;
	}

	const ofColor RSDevice::getColorFromDepthSpace(ofPoint pCameraPoint)
	{
		/**
		if (mCoordinateMapper)
			return getColorFromDepthSpace(pCameraPoint.x, pCameraPoint.y, pCameraPoint.z);
		**/
		return ofColor::black;
	}

		//get ofColor space UVs from a depth image point
	const glm::vec2 RSDevice::getColorCoordsFromDepthImage(float pImageX, float pImageY, float pImageZ)
	{
		/**
		if (mCoordinateMapper)
		{
			PXCPoint3DF32 cPoint;
			cPoint.x = pImageX;
			cPoint.y = pImageY;
			cPoint.z = pImageZ;

			PXCPoint3DF32 *cInPoint = new PXCPoint3DF32[1];
			cInPoint[0] = cPoint;
			PXCPointF32 *cOutPoints = new PXCPointF32[1];
			mCoordinateMapper->MapDepthToColor(1, cInPoint, cOutPoints);

			float cColorX = cOutPoints[0].x;
			float cColorY = cOutPoints[0].y;

			delete cInPoint;
			delete cOutPoints;
			return ofVec2f(cColorX / (float)mRgbSize.x, cColorY / (float)mRgbSize.y);
		}
		**/
		return ofVec2f(0);
	}

	const glm::vec2 RSDevice::getColorCoordsFromDepthImage(int pImageX, int pImageY, uint16_t pImageZ)
	{
		return getColorCoordsFromDepthImage(static_cast<float>(pImageX), static_cast<float>(pImageY), static_cast<float>(pImageZ));
	}

	const glm::vec2 RSDevice::getColorCoordsFromDepthImage(ofPoint pImageCoords)
	{
		return getColorCoordsFromDepthImage(pImageCoords.x, pImageCoords.y, pImageCoords.z);
	}

		//get ofColor space UVs from a depth space point
	const glm::vec2 RSDevice::getColorCoordsFromDepthSpace(float pCameraX, float pCameraY, float pCameraZ)
	{
		/**
		if (mCoordinateMapper)
		{
			PXCPoint3DF32 cPoint;
			cPoint.x = pCameraX; cPoint.y = pCameraY; cPoint.z = pCameraZ;

			PXCPoint3DF32 *cInPoint = new PXCPoint3DF32[1];
			cInPoint[0] = cPoint;
			PXCPointF32 *cOutPoint = new PXCPointF32[1];
			mCoordinateMapper->ProjectCameraToColor(1, cInPoint, cOutPoint);

			ofVec2f cRetPt(cOutPoint[0].x / static_cast<float>(mRgbSize.x), cOutPoint[0].y / static_cast<float>(mRgbSize.y));
			delete cInPoint;
			delete cOutPoint;
			return cRetPt;
		}
		**/
		return glm::vec2();
	}

	const glm::vec2 RSDevice::getColorCoordsFromDepthSpace(ofPoint pCameraPoint)
	{
		return getColorCoordsFromDepthSpace(pCameraPoint.x, pCameraPoint.y, pCameraPoint.z);
	}
}
#pragma endregion