/*
 * ap600u.c
 * - /dev/input/eventX AP600U의 eventX값 찾고 EV_MSC MSC_SCAN 스캔코드 매핑
 * - 0x000c0601: BTN_STYLUS2 1회 DOWN&delay60ms&UP, if BTN_STYLUS=DOWN이면 UP
 * - 0x000c0612: BTN_STYLUS2 DOWN 지속 유지
 * - 0x000c0613: BTN_STYLUS DOWN 지속 유지
 * - /dev/input/event6 (실제 펜) 좌표 및 필압 정상 매핑
 */

// AP600U, 2터치시 버튼1회누름, 아래슬라이드시 다른입력 할때까지 버튼누름, 


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

#ifndef INPUT_PROP_DIRECT
#define INPUT_PROP_DIRECT 0x01
#endif
#ifndef UI_SET_PROPBIT
#define UI_SET_PROPBIT _IOW('U', 110, int)
#endif

#define REMOTE_NAME_KEYWORD "Lenovo Tab Pen Pro"
#define PEN_NAME_KEYWORD    "NVTCapacitivePen"
#define MAX_REMOTES 8

// 사람이 실제 버튼을 눌렀다 떼는 평균 시간 (microsecond 단위: 60,000us = 60ms = 0.06초)
#define HUMAN_CLICK_DELAY_US 60000 

int fd_remotes[MAX_REMOTES];
int remote_count = 0;
int fd_pen = -1;
int fd_uin = -1;

void cleanup(int sig) {
    if (fd_pen >= 0) {
        ioctl(fd_pen, EVIOCGRAB, 0);
        close(fd_pen);
    }
    for (int i = 0; i < remote_count; i++) {
        if (fd_remotes[i] >= 0) {
            ioctl(fd_remotes[i], EVIOCGRAB, 0); // 🚀 리모컨 가로채기 해제 추가
            close(fd_remotes[i]);
        }
    }
    if (fd_uin >= 0) {
        ioctl(fd_uin, UI_DEV_DESTROY);
        close(fd_uin);
    }
    printf("\n[+] 데몬 종료 및 가로채기 해제 완료.\n");
    exit(0);
}

// 펜 하드웨어 노드 탐색
int find_pen_device(const char *keyword) {
    char dev_path[64];
    char dev_name[256];

    for (int i = 0; i < 32; i++) {
        snprintf(dev_path, sizeof(dev_path), "/dev/input/event%d", i);
        // 🚀 EVIOCGRAB 수행을 위해 O_RDWR 권한으로 변경
        int fd = open(dev_path, O_RDWR | O_NONBLOCK);
        if (fd < 0) continue;

        memset(dev_name, 0, sizeof(dev_name));
        if (ioctl(fd, EVIOCGNAME(sizeof(dev_name) - 1), dev_name) >= 0) {
            if (strstr(dev_name, keyword) != NULL) {
                printf("[+] 펜 장치 발견: '%s' -> %s\n", dev_name, dev_path);
                return fd;
            }
        }
        close(fd);
    }
    return -1;
}

// 키워드가 일치하는 모든 리모컨 노드 수집
void find_remote_devices(const char *keyword) {
    char dev_path[64];
    char dev_name[256];

    for (int i = 0; i < 32 && remote_count < MAX_REMOTES; i++) {
        snprintf(dev_path, sizeof(dev_path), "/dev/input/event%d", i);
        // 🚀 EVIOCGRAB 수행을 위해 O_RDWR 권한으로 변경
        int fd = open(dev_path, O_RDWR | O_NONBLOCK);
        if (fd < 0) continue;

        memset(dev_name, 0, sizeof(dev_name));
        if (ioctl(fd, EVIOCGNAME(sizeof(dev_name) - 1), dev_name) >= 0) {
            if (strstr(dev_name, keyword) != NULL && strstr(dev_name, PEN_NAME_KEYWORD) == NULL) {
                printf("[+] 리모컨 노드 연결: '%s' -> %s (fd: %d)\n", dev_name, dev_path, fd);
                fd_remotes[remote_count++] = fd;
                continue;
            }
        }
        close(fd);
    }
}

int main() {
    signal(SIGINT, cleanup);
    signal(SIGTERM, cleanup);

    // 1. 노드 탐색
    fd_pen = find_pen_device(PEN_NAME_KEYWORD);
    if (fd_pen < 0) {
        fprintf(stderr, "[-] 펜 장치('%s')를 찾을 수 없습니다.\n", PEN_NAME_KEYWORD);
        return 1;
    }

    find_remote_devices(REMOTE_NAME_KEYWORD);
    if (remote_count == 0) {
        fprintf(stderr, "[-] 리모컨 장치('%s')를 찾을 수 없습니다.\n", REMOTE_NAME_KEYWORD);
        close(fd_pen);
        return 1;
    }

    // 2. uinput 노드 자동 탐색
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

    // 2. 가상 펜 이름 및 ID 구성
    char dev_name[UINPUT_MAX_NAME_SIZE] = {0};
    if (ioctl(fd_pen, EVIOCGNAME(sizeof(dev_name) - 1), dev_name) < 0) {
        strncpy(dev_name, "NVTCapacitivePen", sizeof(dev_name) - 1);
    }

    struct input_id dev_id;
    if (ioctl(fd_pen, EVIOCGID, &dev_id) < 0) {
        dev_id.bustype = BUS_USB;
        dev_id.vendor  = 0x17ef;
        dev_id.product = 0x61a1;
        dev_id.version = 1;
    }

    // 3. uinput 설정
    ioctl(fd_uin, UI_SET_EVBIT, EV_KEY);
    ioctl(fd_uin, UI_SET_EVBIT, EV_ABS);
    ioctl(fd_uin, UI_SET_EVBIT, EV_SYN);
    ioctl(fd_uin, UI_SET_PROPBIT, INPUT_PROP_DIRECT);

    struct uinput_user_dev uidev;
    memset(&uidev, 0, sizeof(uidev));
    strncpy(uidev.name, dev_name, UINPUT_MAX_NAME_SIZE - 1);
    uidev.id = dev_id;

    // 4. 유효 ABS 축 비트마스크 등록
    uint8_t absbit[(ABS_MAX / 8) + 1] = {0};
    ioctl(fd_pen, EVIOCGBIT(EV_ABS, sizeof(absbit)), absbit);

    for (int axis = 0; axis < ABS_CNT; axis++) {
        if (absbit[axis / 8] & (1 << (axis % 8))) {
            struct input_absinfo absinfo;
            if (ioctl(fd_pen, EVIOCGABS(axis), &absinfo) == 0) {
                ioctl(fd_uin, UI_SET_ABSBIT, axis);
                uidev.absmin[axis]  = absinfo.minimum;
                uidev.absmax[axis]  = absinfo.maximum;
                uidev.absfuzz[axis] = absinfo.fuzz;
                uidev.absflat[axis] = absinfo.flat;
            }
        }
    }

    // 5. KEY 비트마스크 등록
    uint8_t keybit[(KEY_MAX / 8) + 1] = {0};
    ioctl(fd_pen, EVIOCGBIT(EV_KEY, sizeof(keybit)), keybit);
    for (int key = 0; key < KEY_MAX; key++) {
        if (keybit[key / 8] & (1 << (key % 8))) {
            ioctl(fd_uin, UI_SET_KEYBIT, key);
        }
    }
    ioctl(fd_uin, UI_SET_KEYBIT, BTN_TOUCH);
    ioctl(fd_uin, UI_SET_KEYBIT, BTN_TOOL_PEN);
    ioctl(fd_uin, UI_SET_KEYBIT, BTN_STYLUS);
    ioctl(fd_uin, UI_SET_KEYBIT, BTN_STYLUS2);

    // 6. 가상 장치 생성 및 가로채기
    if (write(fd_uin, &uidev, sizeof(uidev)) < 0 || ioctl(fd_uin, UI_DEV_CREATE) < 0) {
        fprintf(stderr, "[-] 가상 장치 생성 실패!\n");
        cleanup(0);
    }

    // 펜 노드 가로채기
    if (ioctl(fd_pen, EVIOCGRAB, 1) < 0) {
        fprintf(stderr, "[-] 펜 장치 EVIOCGRAB 실패!\n");
        cleanup(0);
    }

    // 🚀 원본 입력 차단을 위해 리모컨 노드들도 전부 독점 가로채기(EVIOCGRAB) 추가
    for (int i = 0; i < remote_count; i++) {
        if (ioctl(fd_remotes[i], EVIOCGRAB, 1) < 0) {
            fprintf(stderr, "[-] 경고: 리모컨 노드 %d EVIOCGRAB 실패\n", i);
        }
    }

    printf("[+] 가상 펜 생성 및 리모컨 노드 %d개 가로채기 완료.\n", remote_count);

    // 7. poll 루프 구성
    struct pollfd fds[MAX_REMOTES + 1];
    for (int i = 0; i < remote_count; i++) {
        fds[i].fd = fd_remotes[i];
        fds[i].events = POLLIN;
    }
    int pen_idx = remote_count;
    fds[pen_idx].fd = fd_pen;
    fds[pen_idx].events = POLLIN;

    int stylus2_pressed = 0;
    int stylus_pressed  = 0;

    while (1) {
        int ret = poll(fds, remote_count + 1, -1);
        if (ret < 0) break;

        // [A] 리모컨 이벤트 처리
        for (int i = 0; i < remote_count; i++) {
            if (fds[i].revents & POLLIN) {
                struct input_event ev;
                while (read(fds[i].fd, &ev, sizeof(ev)) > 0) {
                    if (ev.type == EV_MSC && ev.code == MSC_SCAN) {

                        // 1) 0x000c0601: BTN_STYLUS2 1회 클릭
                        if (ev.value == 0x000c0601) {
                            stylus2_pressed = 0;

                            if (stylus_pressed) {
                                stylus_pressed = 0;
                                struct input_event btn_s_up = { .type = EV_KEY, .code = BTN_STYLUS, .value = 0 };
                                struct input_event syn_s    = { .type = EV_SYN, .code = SYN_REPORT, .value = 0 };
                                gettimeofday(&btn_s_up.time, NULL);
                                gettimeofday(&syn_s.time, NULL);
                                write(fd_uin, &btn_s_up, sizeof(btn_s_up));
                                write(fd_uin, &syn_s, sizeof(syn_s));
                            }

                            struct input_event btn_down = { .type = EV_KEY, .code = BTN_STYLUS2, .value = 1 };
                            struct input_event syn1     = { .type = EV_SYN, .code = SYN_REPORT,  .value = 0 };
                            gettimeofday(&btn_down.time, NULL);
                            gettimeofday(&syn1.time, NULL);
                            write(fd_uin, &btn_down, sizeof(btn_down));
                            write(fd_uin, &syn1, sizeof(syn1));

                            usleep(HUMAN_CLICK_DELAY_US);

                            struct input_event btn_up   = { .type = EV_KEY, .code = BTN_STYLUS2, .value = 0 };
                            struct input_event syn2     = { .type = EV_SYN, .code = SYN_REPORT,  .value = 0 };
                            gettimeofday(&btn_up.time, NULL);
                            gettimeofday(&syn2.time, NULL);
                            write(fd_uin, &btn_up, sizeof(btn_up));
                            write(fd_uin, &syn2, sizeof(syn2));
                        }
                        // 2) 0x000c0612: BTN_STYLUS2 DOWN 지속 유지
                        else if (ev.value == 0x000c0612) {
                            stylus2_pressed = 1;
                            struct input_event btn_down = { .type = EV_KEY, .code = BTN_STYLUS2, .value = 1 };
                            struct input_event syn      = { .type = EV_SYN, .code = SYN_REPORT,  .value = 0 };

                            gettimeofday(&btn_down.time, NULL);
                            gettimeofday(&syn.time, NULL);

                            write(fd_uin, &btn_down, sizeof(btn_down));
                            write(fd_uin, &syn, sizeof(syn));
                        }
                        // 3) 0x000c0613: BTN_STYLUS DOWN 지속 유지
                        else if (ev.value == 0x000c0613) {
                            stylus_pressed = 1;
                            struct input_event btn_down = { .type = EV_KEY, .code = BTN_STYLUS, .value = 1 };
                            struct input_event syn      = { .type = EV_SYN, .code = SYN_REPORT, .value = 0 };

                            gettimeofday(&btn_down.time, NULL);
                            gettimeofday(&syn.time, NULL);

                            write(fd_uin, &btn_down, sizeof(btn_down));
                            write(fd_uin, &syn, sizeof(syn));
                        }
                    }
                }
            }
        }

        // [B] 실제 펜 이벤트 처리 및 버튼 주입
        if (fds[pen_idx].revents & POLLIN) {
            struct input_event ev;
            while (read(fd_pen, &ev, sizeof(ev)) > 0) {

                if (ev.type == EV_KEY && ev.code == BTN_STYLUS2) {
                    if (!stylus2_pressed) {
                        write(fd_uin, &ev, sizeof(ev));
                    }
                    continue;
                }
                if (ev.type == EV_KEY && ev.code == BTN_STYLUS) {
                    if (!stylus_pressed) {
                        write(fd_uin, &ev, sizeof(ev));
                    }
                    continue;
                }

                if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
                    if (stylus2_pressed) {
                        struct input_event btn_ev = { .type = EV_KEY, .code = BTN_STYLUS2, .value = 1 };
                        gettimeofday(&btn_ev.time, NULL);
                        write(fd_uin, &btn_ev, sizeof(btn_ev));
                    }
                    if (stylus_pressed) {
                        struct input_event btn_ev = { .type = EV_KEY, .code = BTN_STYLUS, .value = 1 };
                        gettimeofday(&btn_ev.time, NULL);
                        write(fd_uin, &btn_ev, sizeof(btn_ev));
                    }
                }

                write(fd_uin, &ev, sizeof(ev));
            }
        }
    }

    cleanup(0);
    return 0;
}