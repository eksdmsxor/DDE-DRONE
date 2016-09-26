#ifdef _CH_
#pragma package <opencv>
#endif

#define CV_NO_BACKWARD_COMPATIBILITY

#ifndef _EiC
#include "cv.h"
#include "highgui.h"
#include <stdio.h>
#include <ctype.h>
#endif

IplImage *image = 0, *hsv = 0, *hue = 0, *mask = 0, *backproject = 0, *histimg = 0;

CvHistogram *hist = 0; // 히스토그램을 그릴 구조체 선언

int backproject_mode = 0; // backprojection view 변경 판별
int select_object = 0; // 마우스왼쪽클릭 판별
int track_object = 0; // ROI 설정 판별
int show_hist = 1;

CvPoint origin; // 처음 마우스 클릭한 point
CvRect selection; // ROI에 사용될 Rect 선언

CvRect track_window; // ROI하여 cvCamShift 함수의 인자로 사용할 CvRect 타입 선언
CvBox2D track_box; //         cvCamShift 함수의 인자로 사용할 CvBox2D타입 선언

CvConnectedComp track_comp; // camshift 수행결과

int hdims = 16; // 히스토그램 가로길이 16등분

float hranges_arr[] = {0,180}; // (0~180)
float* hranges = hranges_arr; // 히스토그램 생성시 x축 범위 설정

int vmin = 10, vmax = 256, smin = 30; // 3가지 트랙바 초기값 설정

void on_mouse( int event, int x, int y, int flags, void* param )
{
	if( !image )
		return;

	if( image->origin )
		y = image->height - y;

	// 마우스 클릭 후 포인터 이동이 있을 때
	if( select_object )
	{
		selection.x = MIN(x,origin.x);
		selection.y = MIN(y,origin.y);
		selection.width = selection.x + CV_IABS(x - origin.x);
		selection.height = selection.y + CV_IABS(y - origin.y);

		selection.x = MAX( selection.x, 0 );
		selection.y = MAX( selection.y, 0 );
		selection.width = MIN( selection.width, image->width );
		selection.height = MIN( selection.height, image->height );
		selection.width -= selection.x;
		selection.height -= selection.y;
	}

	switch( event )
	{
	// 마우스 왼쪽버튼 클릭
	case CV_EVENT_LBUTTONDOWN:
		origin = cvPoint(x,y);
		selection = cvRect(x,y,0,0);
		select_object = 1;
		break;

	// 마우스 왼쪽버튼 클릭후 release
	case CV_EVENT_LBUTTONUP:
		select_object = 0;
		// ROI 지정되었을 때
		if( selection.width > 0 && selection.height > 0 )
			track_object = -1;
		break;
	}
}


CvScalar hsv2rgb( float hue )
{
	int rgb[3], p, sector;

	static const int sector_data[][3]=
	{{0,2,1}, {1,2,0}, {1,0,2}, {2,0,1}, {2,1,0}, {0,1,2}};

	hue *= 0.033333333333333333333333333333333f;
	sector = cvFloor(hue); // hue값을 버림하여 정수형으로 변환

	p = cvRound(255*(hue - sector));

	// (홀수&1)=1, (짝수&1)=0   sector가 홀수면 : ( sector & 1 ) = 1 ==> p=255
	p ^= sector & 1 ? 255 : 0; // sector가 짝수면 : ( sector & 1 ) = 0 ==> p=0

	rgb[sector_data[sector][0]] = 255;
	rgb[sector_data[sector][1]] = 0;
	rgb[sector_data[sector][2]] = p;

	return cvScalar(rgb[2], rgb[1], rgb[0],0);
}

int main( int argc, char** argv )
{
	CvCapture* capture = cvCaptureFromAVI( "sample.avi" );

	// 캠 연결
	/*if( argc == 1 || (argc == 2 && strlen(argv[1]) == 1 && isdigit(argv[1][0])))
	capture = cvCaptureFromCAM( argc == 2 ? argv[1][0] - '0' : 0 );
	else if( argc == 2 )
	capture = cvCaptureFromAVI( argv[1] );
	*/
	if( !capture )
	{
		fprintf(stderr,"Could not initialize capturing...\n");
		return -1;
	}

	printf( "Hot keys: \n"
		"\tESC - quit the program\n"
		"\tc - stop the tracking\n"
		"\tb - switch to/from backprojection view\n"
		"\th - show/hide object histogram\n"
		"To initialize tracking, select the object with mouse\n" );

	cvNamedWindow( "Histogram", 1 );
	cvNamedWindow( "CamShiftDemo", 1 );

	// 마우스콜백 세팅
	cvSetMouseCallback( "CamShiftDemo", on_mouse, 0 );

	// 트랙바 생성
	cvCreateTrackbar( "Vmin", "CamShiftDemo", &vmin, 256, 0 );
	cvCreateTrackbar( "Vmax", "CamShiftDemo", &vmax, 256, 0 );
	cvCreateTrackbar( "Smin", "CamShiftDemo", &smin, 256, 0 );

	for(;;)
	{
		IplImage* frame = 0;
		int i, bin_w, c;

		frame = cvQueryFrame( capture );

		if( !frame )
			break;

		if( !image ) // 1번만 실행
		{
			/* allocate all the buffers */
			image = cvCreateImage( cvGetSize(frame), 8, 3 );
			// origin(=좌표의 원점위치 설정), 0-좌상, 1-좌하
			image->origin = frame->origin;

			// 3채널 공간 생성 : hue(색상) saturation(채도) value(명도)
			hsv = cvCreateImage( cvGetSize(frame), 8, 3 );

			// 1채널 공간 생성
			hue = cvCreateImage( cvGetSize(frame), 8, 1 );
			mask = cvCreateImage( cvGetSize(frame), 8, 1 );
			backproject = cvCreateImage( cvGetSize(frame), 8, 1 );

			// 히스토그램 생성 (채널, size, 표현방식, x축범위, 막대간격)
			hist = cvCreateHist( 1, &hdims, CV_HIST_ARRAY, &hranges, 1 );
			// 히스토그램 출력할 공간 생성 후 초기화
			histimg = cvCreateImage( cvSize(320,200), 8, 3 );
			cvZero( histimg );
		}

		// frame을 image로 복사
		cvCopy( frame, image, 0 );

		// 복사한 image(RGB)를 hsv(HSV)로 변환
		cvCvtColor( image, hsv, CV_BGR2HSV );

		// 
		if( track_object )
		{
			//  _vmin(10),    _vmax(256)
			int _vmin = vmin, _vmax = vmax;
			// value채널의 max, min, saturation채널의 min
			// hsv에서 범위내 영역을 추출하여 mask로 보낸다
			cvInRangeS( hsv, cvScalar(0,smin,MIN(_vmin,_vmax),0), cvScalar(180,256,MAX(_vmin,_vmax),0), mask );

			// hsv(3)채널의 h를 hue(1)채널로 뿌린다
			cvSplit( hsv, hue, 0, 0, 0 );

			// 히스토그램 출력
			// track_object = -1 일 때(ROI 지정되었을 때) 실행
			if( track_object < 0 )
			{
				// 히스토그램의 최대 빈도수를 저장할 변수 선언
				float max_val = 0.f;

				// 마우스로 선택한 부분을 ROI 설정
				cvSetImageROI( hue, selection );
				cvSetImageROI( mask, selection );

				// 히스토그램 연산결과를 hist에 저장
				cvCalcHist( &hue, hist, 0, mask );

				// 히스토그램에서 최대 빈도수를 max_val에 저장
				cvGetMinMaxHistValue( hist, 0, &max_val, 0, 0 );

				// (조건)?A:B
				// max_val(최대빈도수) 존재하면 255/max_val값, 아니면 0값을 소스영상에 곱한다
				// (dst) = (src)*scale + shift
				// 소스영상에 스케일, 시프트 연산 수행 후 배열을 다른 배열로 선형변환
				cvConvertScale( hist->bins, hist->bins, max_val ? 255. / max_val : 0., 0 );

				// ROI 해제
				cvResetImageROI( hue );
				cvResetImageROI( mask );

				// 마우스로 선택한 부분을 track_window에 저장
				track_window = selection;
				// track_object 값(-1) 변화
				track_object = 1;

				cvZero( histimg );
				// 히스토그램 표시할 이미지 가로 영역을 16등분한 크기 (hdims=16)
				bin_w = histimg->width / hdims;

				for( i = 0; i < hdims; i++ )
				{
					// (i번째 막대의 빈도수)*(히스토그램 표시할 이미지 세로 영역을 255등분한 크기) 반올림
					// (빈도수=0)이라면 (val=0)
					int val = cvRound( cvGetReal1D(hist->bins,i)*histimg->height/255 );

					// HSV2RGB 변환후 색 지정
					CvScalar color = hsv2rgb(i*180.f/hdims);

					// 히스토그램 막대 각각 출력(대각선 모서리 방향 : ↗)
					// (빈도수=0)이라서 (val=0)이면
					// (height-val=height)이므로 (point1 height=point2 height) --> 막대 안 그림
					cvRectangle( histimg, cvPoint(i*bin_w,histimg->height), cvPoint((i+1)*bin_w,histimg->height - val),
					color, -1, 8, 0 );
				}
			}

			// 히스토그램의 최대빈도수 특성을 가지고 영상을 다시 그림
			cvCalcBackProject( &hue, backproject, hist );

			// backproject와 mask를 AND연산
			cvAnd( backproject, mask, backproject, 0 );

			// camshift연산
			cvCamShift( backproject, track_window,
			cvTermCriteria( CV_TERMCRIT_EPS | CV_TERMCRIT_ITER, 10, 1 ), // 종료조건 : (반복10회이상, 정확도=1)
			&track_comp, &track_box );

			// camshift연산한 부분을 계속 track_window에 넘겨줌
			track_window = track_comp.rect;

			// 'b'를 눌러서 backprojection view 변경을 선택했을 때
			if( backproject_mode )
				// 화면 흑백화
				cvCvtColor( backproject, image, CV_GRAY2BGR );

			// ①-②
			// |　 |
			// ③-④  ==> angle : 직선 ①-②와 x축이 이루는 각도(0~90˚)
			if( !image->origin )
				track_box.angle = -track_box.angle;

			// CvBox2D로 표현된 회전된 사각형 영역에 타원을 그림
			cvEllipseBox( image, track_box, CV_RGB(255,0,0), 3, CV_AA, 0 );
		}

		// 마우스로 드래그한 ROI의 색을 반전
		// (마우스 왼쪽 버튼을 클릭중 & 마우스 드래그하여 ROI 지정중) 일 때
		if( select_object && selection.width > 0 && selection.height > 0 )
		{
			cvSetImageROI( image, selection );
			// 행렬과 White XOR연산 수행 - 색 반전 효과
			cvXorS( image, cvScalarAll(255), image, 0 );
			cvResetImageROI( image );
		}

		cvShowImage( "CamShiftDemo", image );
		cvShowImage( "Histogram", histimg );

		c = cvWaitKey(10);

		// 'esc' : 프로그램 종료
		if( (char) c == 27 )
			break;

		switch( (char) c )
		{
		// 'b' : backprojection view 변경
		// (0)XOR(1)=(1), (1)XOR(1)=(0), 누를 때 마다 toggle
		case 'b':
			backproject_mode ^= 1;
			break;

		// 'c' : tracking 종료
		case 'c':
			track_object = 0;
			cvZero( histimg );
			break;

		// 'h' : 히스토그램창 종료
		case 'h':
			show_hist ^= 1;
			if( !show_hist )
			cvDestroyWindow( "Histogram" );
			else
			cvNamedWindow( "Histogram", 1 );
			break;

		default:
			break;
		}
	}

	cvReleaseCapture( &capture );
	cvDestroyWindow("CamShiftDemo");

	return 0;
}