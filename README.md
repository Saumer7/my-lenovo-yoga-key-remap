# my-lenovo-yoga-key-remap
레노버 요가패드12.7의 전용키보드와 펜버튼 리맵핑.

keyboard : 전용키보드의 맨 윗 버튼들을 F1~F12로 리맵핑하고, Meta키를 누른 상태에선 원본키로 작동하게.  
keyboard_keymapper앱 설정&Autohotkey설정 : 원격PC사용 중 Meta→LWin RAlt→한영 RCtrl→Menu 으로 리맵핑, LAlt+Tab 작동케 함.    
ap600u : 2번터치시 BTN_Stylus2클릭&모든버튼뗌, 아래슬라이드시 BTN_Stylus2유지, 위슬라이드시 BTN_Stylus1유지  

100% AI 바이브코딩 결과물입니다. 본인은 코드 1줄도 못읽고 못씁니다. 코드가 적절하지 않을지도 모릅니다.  
실행파일 keyboard와 ap600u의 소스코드는 최상단폴더의 keyboard.c , ap600u.c 입니다.

사용법 :  
Magisk모듈 설치 후 재부팅 또는 모듈 설치파일 내 컴파일된 파일(keyboard, ap600u) 실행권한 준 뒤 터미널앱에서 실행.
키보드 Magisk모듈의 경우 비정상작동시 정상작동할 때까지 키보드 재연결&대기5초
