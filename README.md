# lenovo-yoga12.7-key-remap
레노버 요가패드12.7(TB520FU) 버튼 리매핑.

### 키보드
keymapper앱 설정 & Autohotkey스크립트 : 원격PC사용 중 Meta→LWin, RAlt→한영, RCtrl→Menu 리매핑. LAlt+Tab 작동케 함. 
데몬파일 keyboard : ESC옆12키 → F1~F12 , Meta키를 누른 상태에선 원본키로 라매핑 (KeyMapper expert mode 켜진 상태에서만 작동).  

### 펜
데몬파일 ap600u : 2번터치→BTN_Stylus2클릭&BTN_Stylus1뗌, 아래슬라이드→BTN_Stylus2유지, 위슬라이드→BTN_Stylus1유지  

---

### 사용법
Magisk모듈 설치 후 재부팅. 또는 컴파일된 파일(keyboard, ap600u) 실행권한 준 뒤 터미널앱에서 실행.  
키보드 Keymapper 설정파일은 해당 앱에서 import (expertmode 활성화 필요), .ahk파일은 Autohotkey 스크립트로 PC에서 Autohotkey로 실행.

---

100% AI 바이브코딩 결과물입니다. 본인은 코드 1줄도 못읽고 못씁니다. 코드가 적절하지 않을지도 모릅니다.  
실행파일 keyboard와 ap600u의 소스코드는 최상단폴더의 keyboard.c , ap600u.c 입니다.  
