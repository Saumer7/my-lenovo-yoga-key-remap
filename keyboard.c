#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/poll.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/inotify.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <errno.h>
#include <stdint.h>

#define KEYBOARD_NAME_KEYWORD "Lenovo Keyboard Pack For Yoga Tab Keyboard"
#define MAX_KEYBOARDS 8

int fd_keyboards[MAX_KEYBOARDS];
int keyboard_count = 0;
int fd_uin = -1;

int current_mapped_key = -1;
int leftmeta_pressed = 0;
int leftmeta_forwarded = 0;

// 세션 정리 (연결 해제 시 호출)
void close_session() {
    for (int i = 0; i < keyboard_count; i++) {
        if (fd_keyboards[i] >= 0) {
            ioctl(fd_keyboards[i], EVIOCGRAB, 0);
            close(fd_keyboards[i]);
            fd_keyboards[i] = -1;
        }
    }
    keyboard_count = 0;

    if (fd_uin >= 0) {
        ioctl(fd_uin, UI_DEV_DESTROY);
        close(fd_uin);
        fd_uin = -1;
    }

    current_mapped_key = -1;
    leftmeta_pressed = 0;
    leftmeta_forwarded = 0;
}

void cleanup(int sig) {
    close_session();
    printf("\n[+] 데몬 종료 및 가로채기 해제 완료.\n");
    exit(0);
}

// 🚀 inotify 기반 /dev/input 변경 이벤트 대기 함수 (CPU 점유율 0%)
void wait_for_input_change() {
    int inotify_fd = inotify_init();
    if (inotify_fd < 0) {
        sleep(2);
        return;
    }

    int wd = inotify_add_watch(inotify_fd, "/dev/input", IN_CREATE | IN_ATTRIB);
    if (wd >= 0) {
        char buf[512];
        read(inotify_fd, buf, sizeof(buf)); // 신규 장치 연결 시까지 무한 대기
        inotify_rm_watch(inotify_fd, wd);
    }
    close(inotify_fd);
}

// 키보드 노드 탐색 (EVIOCGRAB을 위해 O_RDWR 권한으로 열기)
void find_keyboard_devices(const char *keyword) {
    char dev_path[64];
    char dev_name[256];

    for (int i = 0; i < 32 && keyboard_count < MAX_KEYBOARDS; i++) {
        snprintf(dev_path, sizeof(dev_path), "/dev/input/event%d", i);
        int fd = open(dev_path, O_RDWR | O_NONBLOCK); // 🚀 O_RDWR로 변경
        if (fd < 0) continue;

        memset(dev_name, 0, sizeof(dev_name));
        if (ioctl(fd, EVIOCGNAME(sizeof(dev_name) - 1), dev_name) >= 0) {
            if (strstr(dev_name, keyword) != NULL) {
                fd_keyboards[keyboard_count++] = fd;
                continue;
            }
        }
        close(fd);
    }
}

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

int setup_uinput() {
    const char *uinput_paths[] = {"/dev/uinput", "/dev/misc/uinput"};
    for (int i = 0; i < 2; i++) {
        fd_uin = open(uinput_paths[i], O_WRONLY | O_NONBLOCK);
        if (fd_uin >= 0) break;
    }
    if (fd_uin < 0) return -1;

    ioctl(fd_uin, UI_SET_EVBIT, EV_KEY);
    ioctl(fd_uin, UI_SET_EVBIT, EV_REP);
    ioctl(fd_uin, UI_SET_EVBIT, EV_SYN);
    ioctl(fd_uin, UI_SET_EVBIT, EV_MSC);
    ioctl(fd_uin, UI_SET_MSCBIT, MSC_SCAN);

    for (int i = 0; i < keyboard_count; i++) {
        uint8_t keybit[(KEY_MAX / 8) + 1] = {0};
        ioctl(fd_keyboards[i], EVIOCGBIT(EV_KEY, sizeof(keybit)), keybit);
        for (int key = 0; key < KEY_MAX; key++) {
            if (keybit[key / 8] & (1 << (key % 8))) {
                ioctl(fd_uin, UI_SET_KEYBIT, key);
            }
        }
    }

    for (int k = KEY_F1; k <= KEY_F12; k++) {
        ioctl(fd_uin, UI_SET_KEYBIT, k);
    }

    struct input_id orig_id;
    memset(&orig_id, 0, sizeof(orig_id));
    if (ioctl(fd_keyboards[0], EVIOCGID, &orig_id) < 0) {
        orig_id.bustype = BUS_USB;
        orig_id.vendor  = 0x17ef;
        orig_id.product = 0x61a2;
        orig_id.version = 1;
    }

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
        return -1;
    }
    return 0;
}

int main() {
    signal(SIGINT, cleanup);
    signal(SIGTERM, cleanup);

    printf("[+] inotify 기반 키보드 F1~F12 리매퍼 자동 대기 데몬 시작\n");

    while (1) {
        // 1. 기존 세션 정돈 및 장치 탐색
        close_session();
        find_keyboard_devices(KEYBOARD_NAME_KEYWORD);

        // 2. 키보드가 미연결 상태면 inotify 대기 (CPU 점유율 0%)
        if (keyboard_count == 0) {
            wait_for_input_change();
            continue;
        }
		
		// 키보드 연결시 다른 서비스(Key Mapper expert mode)가 선점할 수 있도록 대기
        printf("[+] 키보드 감지됨. KeyMapper가 먼저 선점하도록 2초간 대기합니다.\n");
        sleep(2);

        // 3. uinput 생성 및 원본 키보드 독점 가로채기
        if (setup_uinput() < 0) {
            wait_for_input_change();
            continue;
        }

        for (int i = 0; i < keyboard_count; i++) {
            if (ioctl(fd_keyboards[i], EVIOCGRAB, 1) < 0) {
                fprintf(stderr, "[-] 경고: 키보드 노드 %d EVIOCGRAB 실패(keymapper expert mode때문이면 의도대로 작동한 것임)\n지금 F5~F6키, META+F5~F6키가 정상 작동하지 않으면 키보드를 재연결하세요\n", i);
            }
        }
		
        printf("[+] 키보드 연결 감지: '%s' (노드 수: %d개) 가로채기 완료.\n", KEYBOARD_NAME_KEYWORD, keyboard_count);

        // 4. 폴링 및 이벤트 리매핑 루프
        struct pollfd fds[MAX_KEYBOARDS];
        int disconnected = 0;

        while (!disconnected) {
            for (int i = 0; i < keyboard_count; i++) {
                fds[i].fd = fd_keyboards[i];
                fds[i].events = POLLIN;
            }

            int ret = poll(fds, keyboard_count, -1);
            if (ret < 0) break;

            for (int i = 0; i < keyboard_count; i++) {
                if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                    disconnected = 1;
                    break;
                }

                if (fds[i].revents & POLLIN) {
                    struct input_event ev;
                    while (read(fds[i].fd, &ev, sizeof(ev)) > 0) {

                        if (ev.type == EV_MSC && ev.code == MSC_SCAN) {
                            current_mapped_key = get_mapped_key_from_scan(ev.value);
                            if (leftmeta_pressed) {
                                release_virtual_meta(ev.time);
                                write(fd_uin, &ev, sizeof(ev));
                            }
                        }
                        else if (ev.type == EV_KEY) {
                            if (ev.code == KEY_LEFTMETA) {
                                leftmeta_pressed = (ev.value != 0);

                                if (ev.value != 0) {
                                    write(fd_uin, &ev, sizeof(ev));
                                    leftmeta_forwarded = 1;
                                } else {
                                    if (leftmeta_forwarded) {
                                        write(fd_uin, &ev, sizeof(ev));
                                    }
                                    leftmeta_forwarded = 0;
                                }
                                continue;
                            }

                            if (leftmeta_pressed) {
                                release_virtual_meta(ev.time);
                                current_mapped_key = -1;
                                write(fd_uin, &ev, sizeof(ev));
                                continue;
                            }

                            if (ev.code == KEY_MUTE)            ev.code = KEY_F1;
                            else if (ev.code == KEY_VOLUMEDOWN) ev.code = KEY_F2;
                            else if (ev.code == KEY_VOLUMEUP)   ev.code = KEY_F3;
                            else if (ev.code == KEY_SYSRQ)      ev.code = KEY_F8;
                            else if (current_mapped_key != -1) {
                                ev.code = current_mapped_key;
                                current_mapped_key = -1;
                            }

                            write(fd_uin, &ev, sizeof(ev));
                        }
                        else {
                            write(fd_uin, &ev, sizeof(ev));
                        }
                    }
                }
            }
        }

        printf("[!] 키보드 연결 해제 감지. 신규 장치 연결 대기 중...\n");
    }

    return 0;
}