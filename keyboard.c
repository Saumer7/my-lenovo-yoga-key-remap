#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/poll.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <errno.h>
#include <stdint.h>

#define KEYBOARD_NAME_KEYWORD "Lenovo Keyboard Pack For Yoga Tab Keyboard"
#define MAX_KEYBOARDS 8

int fd_keyboards[MAX_KEYBOARDS];
int keyboard_count = 0;
int fd_uin = -1;

// 최근 감지된 스캔코드에 매핑된 F키를 기억하기 위한 변수
int current_mapped_key = -1;

// LEFTMETA 키가 현재(물리적으로) 눌려있는 상태인지 추적 (눌려있으면 리매핑을 건너뛰고 원본 키 사용)
int leftmeta_pressed = 0;

// LEFTMETA가 "가상 장치 상에서" 현재 눌린 것으로 보고돼 있는 상태인지 추적.
// Keymapper 같은 다른 앱이 Meta_Left를 감지/리매핑할 수 있도록 평소엔 Meta를
// 가상 장치로도 그대로 전달하되, F1~F12 우회 로직이 발동하는 순간에는 안드로이드가
// "가상 장치 + 모디파이어 조합"을 걸러내는 것을 피하기 위해 여기서만 미리 떼준다.
int leftmeta_forwarded = 0;

// leftmeta_forwarded가 1이면, 가상 장치 상에서 LEFTMETA UP을 미리 보내
// 실제로는 안 눌린 것처럼 만든다. (F1~F12 우회 로직 진입 직전에 호출)
void release_virtual_meta(struct timeval ts) {
    if (!leftmeta_forwarded) return;

    struct input_event up_ev;
    memset(&up_ev, 0, sizeof(up_ev));
    up_ev.time = ts;
    up_ev.type = EV_KEY;
    up_ev.code = KEY_LEFTMETA;
    up_ev.value = 0;
    write(fd_uin, &up_ev, sizeof(up_ev));

    struct input_event syn_ev;
    memset(&syn_ev, 0, sizeof(syn_ev));
    syn_ev.time = ts;
    syn_ev.type = EV_SYN;
    syn_ev.code = SYN_REPORT;
    syn_ev.value = 0;
    write(fd_uin, &syn_ev, sizeof(syn_ev));

    leftmeta_forwarded = 0;
}

void cleanup(int sig) {
    for (int i = 0; i < keyboard_count; i++) {
        if (fd_keyboards[i] >= 0) {
            ioctl(fd_keyboards[i], EVIOCGRAB, 0); // 그랩 해제
            close(fd_keyboards[i]);
        }
    }
    if (fd_uin >= 0) {
        ioctl(fd_uin, UI_DEV_DESTROY);
        close(fd_uin);
    }
    printf("\n[+] 데몬 종료 및 가로채기 해제 완료.\n");
    exit(0);
}

// 키보드(키패드) 노드 수집
void find_keyboard_devices(const char *keyword) {
    char dev_path[64];
    char dev_name[256];

    for (int i = 0; i < 32 && keyboard_count < MAX_KEYBOARDS; i++) {
        snprintf(dev_path, sizeof(dev_path), "/dev/input/event%d", i);
        int fd = open(dev_path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;

        memset(dev_name, 0, sizeof(dev_name));
        if (ioctl(fd, EVIOCGNAME(sizeof(dev_name) - 1), dev_name) >= 0) {
            if (strstr(dev_name, keyword) != NULL) {
                printf("[+] 키보드 노드 연결: '%s' -> %s (fd: %d)\n", dev_name, dev_path, fd);
                fd_keyboards[keyboard_count++] = fd;
                continue;
            }
        }
        close(fd);
    }
}

// 스캔코드를 F키로 매핑하는 함수
int get_mapped_key_from_scan(unsigned int scan_code) {
    switch (scan_code) {
        case 0x000c0391: return KEY_F4;
        case 0x000c0070: return KEY_F5;
        case 0x000c006f: return KEY_F6;
        case 0x000c0392: return KEY_F7;
        case 0x000c0394: return KEY_F9;
        case 0x000c0395: return KEY_F10;
        case 0x000c0397: return KEY_F11;
        case 0x000c038e: return KEY_F12;
        default: return -1;
    }
}

int main() {
    signal(SIGINT, cleanup);
    signal(SIGTERM, cleanup);

    // 1. 키보드 노드 탐색
    find_keyboard_devices(KEYBOARD_NAME_KEYWORD);
    if (keyboard_count == 0) {
        fprintf(stderr, "[-] 키보드 장치('%s')를 찾을 수 없습니다.\n", KEYBOARD_NAME_KEYWORD);
        return 1;
    }

    // 2. uinput 노드 자동 탐색 (기기 호환성 강화)
    const char *uinput_paths[] = {"/dev/uinput", "/dev/misc/uinput"};
    for (int i = 0; i < 2; i++) {
        fd_uin = open(uinput_paths[i], O_WRONLY | O_NONBLOCK);
        if (fd_uin >= 0) {
            printf("[+] uinput 노드 열기 성공: %s\n", uinput_paths[i]);
            break;
        }
    }

    if (fd_uin < 0) {
        fprintf(stderr, "[-] /dev/uinput 열기 실패! (원인: %s)\n", strerror(errno));
        fprintf(stderr, "    -> 'su' 권한 실행 여부 및 'setenforce 0' 명령을 확인하세요.\n");
        cleanup(0);
    }

    // 3. uinput 설정 (이벤트 타입 등록)
    ioctl(fd_uin, UI_SET_EVBIT, EV_KEY);
    ioctl(fd_uin, UI_SET_EVBIT, EV_REP);
    ioctl(fd_uin, UI_SET_EVBIT, EV_SYN);

    // EV_MSC / MSC_SCAN 지원 선언 (필수)
    // 이걸 등록하지 않으면 커널 uinput 드라이버가 MSC_SCAN write()를 조용히
    // 버리기 때문에, "key usage 0x0c...." 형태로 정의된 .kl 매핑
    // (밝기조절/마이크토글/터치패드토글 등)이 안드로이드에서 전혀 인식되지 않는다.
    ioctl(fd_uin, UI_SET_EVBIT, EV_MSC);
    ioctl(fd_uin, UI_SET_MSCBIT, MSC_SCAN);

    // 원본 키보드가 가진 키 비트 복사
    for (int i = 0; i < keyboard_count; i++) {
        uint8_t keybit[(KEY_MAX / 8) + 1] = {0};
        ioctl(fd_keyboards[i], EVIOCGBIT(EV_KEY, sizeof(keybit)), keybit);
        for (int key = 0; key < KEY_MAX; key++) {
            if (keybit[key / 8] & (1 << (key % 8))) {
                ioctl(fd_uin, UI_SET_KEYBIT, key);
            }
        }
    }

    // F1 ~ F12 전체 강제 비트 등록
    for (int k = KEY_F1; k <= KEY_F12; k++) {
        ioctl(fd_uin, UI_SET_KEYBIT, k);
    }

    // 4. 가상 장치 생성
    // 원본 키보드의 vendor/product/bustype을 그대로 읽어와 사용한다.
    // (안드로이드는 vendor+product ID로 Vendor_XXXX_Product_YYYY.kl 키레이아웃 파일을
    //  찾기 때문에, ID가 다르면 원본 키보드 전용 키레이아웃을 못 찾고 Generic.kl로
    //  폴백되어 표준 키(KEY_SYSRQ 등)만 동작하고 나머지 벤더 고유 스캔코드 키는
    //  이벤트는 들어오지만 실제 기능으로 연결되지 않는다.)
    struct input_id orig_id;
    memset(&orig_id, 0, sizeof(orig_id));
    if (ioctl(fd_keyboards[0], EVIOCGID, &orig_id) < 0) {
        fprintf(stderr, "[-] 경고: 원본 키보드 ID 조회 실패, 기본값 사용\n");
        orig_id.bustype = BUS_USB;
        orig_id.vendor  = 0x17ef;
        orig_id.product = 0x61a2;
        orig_id.version = 1;
    } else {
        printf("[+] 원본 키보드 ID: bustype=0x%04x vendor=0x%04x product=0x%04x version=0x%04x\n",
               orig_id.bustype, orig_id.vendor, orig_id.product, orig_id.version);
    }

    // 원본 장치 이름도 동일하게 사용 (vendor/product가 0인 예외적인 경우
    // 안드로이드가 이름 기반 해시로 키레이아웃을 찾기 때문에 대비 차원에서 맞춰준다)
    char orig_name[256];
    memset(orig_name, 0, sizeof(orig_name));
    if (ioctl(fd_keyboards[0], EVIOCGNAME(sizeof(orig_name) - 1), orig_name) < 0) {
        strncpy(orig_name, "Lenovo Keyboard Pack - F1-F12 Remapper", sizeof(orig_name) - 1);
    }

    struct uinput_user_dev uidev;
    memset(&uidev, 0, sizeof(uidev));
    strncpy(uidev.name, orig_name, UINPUT_MAX_NAME_SIZE - 1);
    uidev.id.bustype = orig_id.bustype;
    uidev.id.vendor  = orig_id.vendor;
    uidev.id.product = orig_id.product;
    uidev.id.version = orig_id.version;

    if (write(fd_uin, &uidev, sizeof(uidev)) < 0 || ioctl(fd_uin, UI_DEV_CREATE) < 0) {
        fprintf(stderr, "[-] 가상 키보드 장치 생성 실패!\n");
        cleanup(0);
    }

    // 5. 원본 장치 독점 (Grab)
    for (int i = 0; i < keyboard_count; i++) {
        if (ioctl(fd_keyboards[i], EVIOCGRAB, 1) < 0) {
            fprintf(stderr, "[-] 경고: 키보드 노드 %d EVIOCGRAB 실패\n", i);
        }
    }
    
    printf("[+] 가상 키보드 생성 및 매핑 완료 (연결된 키보드 노드 수: %d개, F1~F12 전체 지원).\n", keyboard_count);

    // 6. 이벤트 폴링 및 리매핑 루프
    struct pollfd fds[MAX_KEYBOARDS];
    for (int i = 0; i < keyboard_count; i++) {
        fds[i].fd = fd_keyboards[i];
        fds[i].events = POLLIN;
    }

    while (1) {
        int ret = poll(fds, keyboard_count, -1);
        if (ret < 0) break;

        for (int i = 0; i < keyboard_count; i++) {
            if (fds[i].revents & POLLIN) {
                struct input_event ev;
                while (read(fds[i].fd, &ev, sizeof(ev)) > 0) {
                    
                    // [수정된 부분]: EV_MSC(스캔코드) 처리
                    if (ev.type == EV_MSC && ev.code == MSC_SCAN) {
                        current_mapped_key = get_mapped_key_from_scan(ev.value);

                        // 안드로이드는 MSC_SCAN(usage) 값이 존재하면 .kl의
                        // "key usage 0x0c.... XXXX" 매핑을 keycode 기반 매핑보다
                        // 우선 적용한다. 그래서:
                        //  - 리매핑 모드(Meta 안 누름): MSC_SCAN을 전달하지 않아야
                        //    usage 매핑에 안 걸리고, 우리가 뒤에서 설정하는
                        //    keycode(F4~F12)가 그대로 인식된다.
                        //  - Meta 우회 모드(Meta 누름): 원본 기능을 살리기 위해
                        //    MSC_SCAN을 그대로 전달해서 usage 매핑이 적용되게 한다.
                        if (leftmeta_pressed) {
                            // 우회 로직 발동 직전이므로, 가상 장치 상의 Meta를
                            // 미리 떼서 "모디파이어 없는 단일 키"로 보이게 한다.
                            release_virtual_meta(ev.time);
                            write(fd_uin, &ev, sizeof(ev));
                        }
                    }
                    // [수정된 부분]: EV_KEY 처리 (조건문 통합)
                    else if (ev.type == EV_KEY) {

                        // 0) LEFTMETA 자체의 눌림/뗌 상태를 갱신
                        //    (ev.value: 0=뗌, 1=누름, 2=반복)
                        if (ev.code == KEY_LEFTMETA) {
                            leftmeta_pressed = (ev.value != 0);

                            if (ev.value != 0) {
                                // 눌림: 평소엔 그대로 가상 장치에도 전달해서
                                // Keymapper 같은 다른 앱이 Meta_Left를 감지/리매핑할 수 있게 한다.
                                write(fd_uin, &ev, sizeof(ev));
                                leftmeta_forwarded = 1;
                            } else {
                                // 뗌: 우회 로직 발동으로 가상 장치 상에서 이미
                                // 뗀 상태(leftmeta_forwarded==0)라면 중복 전달하지 않는다.
                                if (leftmeta_forwarded) {
                                    write(fd_uin, &ev, sizeof(ev));
                                }
                                leftmeta_forwarded = 0;
                            }
                            continue;
                        }

                        // LEFTMETA가 눌려있는 동안에는 리매핑을 하지 않고 원본 키코드 그대로 전달
                        if (leftmeta_pressed) {
                            // 가상 장치 상의 Meta를 미리 떼서(이미 안 뗀 상태라면) 순수
                            // 단일 키 입력으로 보이게 한다.
                            release_virtual_meta(ev.time);

                            // MSC_SCAN으로 예약해둔 매핑 정보가 있다면, 다음 키 입력에
                            // 잘못 영향을 주지 않도록 여기서 초기화만 해준다.
                            current_mapped_key = -1;
                            write(fd_uin, &ev, sizeof(ev));
                            continue;
                        }

                        // 1) 직접 매핑 (우선순위 높음)
                        if (ev.code == KEY_MUTE)              ev.code = KEY_F1;
                        else if (ev.code == KEY_VOLUMEDOWN)   ev.code = KEY_F2;
                        else if (ev.code == KEY_VOLUMEUP)     ev.code = KEY_F3;
                        else if (ev.code == KEY_SYSRQ)        ev.code = KEY_F8;
                        
                        // 2) MSC_SCAN으로 식별된 매핑 (F4~7, F9~12)
                        else if (current_mapped_key != -1) {
                            ev.code = current_mapped_key;
                            
                            // 키 꼬임(Ghosting) 방지를 위해 이벤트 변조 후 즉시 초기화
                            // (누를 때, 반복될 때, 뗄 때 항상 MSC_SCAN이 선행되어 들어옴)
                            current_mapped_key = -1;
                        }
                        
                        write(fd_uin, &ev, sizeof(ev));
                    }
                    // [수정된 부분]: 그 외 이벤트 (SYN 등) 통과
                    else {
                        write(fd_uin, &ev, sizeof(ev));
                    }
                }
            }
        }
    }

    cleanup(0);
    return 0;
}
