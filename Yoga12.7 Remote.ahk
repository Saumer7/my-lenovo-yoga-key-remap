; AutoHotkey v2

; 관리자 권한으로 자동 실행 설정
if !A_IsAdmin {
    Run "*RunAs " . DllCall("GetCommandLine", "str")
    ExitApp
}



; NumpadDiv→LWin
NumpadDiv::LWin

; Numpad0→한영
; Numpad는 Numlock에 따라 값이 2개이므로 규칙이 2개 필요
NumpadIns:: {
	Send "{vk15sc1F2}"
}
Numpad0:: {
	Send "{vk15sc1F2}"
}

;Numpad2→menu
Numpad2::AppsKey
NumpadDown::AppsKey
