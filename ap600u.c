/*
 * ap600u.c
 * - /dev/input/eventX AP600U의 eventX값 찾고 EV_MSC MSC_SCAN 스캔코드 매핑
 * - 0x000c0601: BTN_STYLUS2 1회 DOWN&delay60ms&UP, if BTN_STYLUS=DOWN이면 UP
 * - 0x000c0612: BTN_STYLUS2 DOWN 지속 유지
 * - 0x000c0613: BTN_STYLUS DOWN 지속 유지
 * - /dev/input/event6 (실제 펜) 좌표 및 필압 정상 매핑
 */

// AP600U, 2터치시 버튼1회누름, 아래슬라이드시 다른입력 할때까지 버튼누름, 




/*
 * ap600u_auto_inotify.c
 * - inotify 기반 zero-latency / zero-battery 블루투스 재연결 데몬
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/poll.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/inotify.h> // inotify 헤더 추가
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
#define HUMAN_CLICK_DELAY_US 60000 

int fd_remotes[MAX_REMOTES];
int remote_count = 0;
int fd_pen = -1;
int fd_uin = -1;

void close_session() {
    if (fd_pen >= 0) {
        ioctl(fd_pen, EVIOCGRAB, 0);
        close(fd_pen);
        fd_pen = -1;
    }
    for (int i = 0; i < remote_count; i++) {
        if (fd_remotes[i] >= 0) {
            ioctl(fd_remotes[i], EVIOCGRAB, 0);
            close(fd_remotes[i]);
            fd_remotes[i] = -1;
        }
    }
    remote_count = 0;
    if (fd_uin >= 0) {
        ioctl(fd_uin, UI_DEV_DESTROY);
        close(fd_uin);
        fd_uin = -1;
    }
}

void cleanup(int sig) {
    close_session();
    printf("\n[+] 데몬 종료 완료.\n");
    exit(0);
}

int find_pen_device(const char *keyword) {
    char dev_path[64], dev_name[256];
    for (int i = 0; i < 32; i++) {
        snprintf(dev_path, sizeof(dev_path), "/dev/input/event%d", i);
        int fd = open(dev_path, O_RDWR | O_NONBLOCK);
        if (fd < 0) continue;

        memset(dev_name, 0, sizeof(dev_name));
        if (ioctl(fd, EVIOCGNAME(sizeof(dev_name) - 1), dev_name) >= 0) {
            if (strstr(dev_name, keyword) != NULL) return fd;
        }
        close(fd);
    }
    return -1;
}

void find_remote_devices(const char *keyword) {
    char dev_path[64], dev_name[256];
    for (int i = 0; i < 32 && remote_count < MAX_REMOTES; i++) {
        snprintf(dev_path, sizeof(dev_path), "/dev/input/event%d", i);
        int fd = open(dev_path, O_RDWR | O_NONBLOCK);
        if (fd < 0) continue;

        memset(dev_name, 0, sizeof(dev_name));
        if (ioctl(fd, EVIOCGNAME(sizeof(dev_name) - 1), dev_name) >= 0) {
            if (strstr(dev_name, keyword) != NULL && strstr(dev_name, PEN_NAME_KEYWORD) == NULL) {
                fd_remotes[remote_count++] = fd;
                continue;
            }
        }
        close(fd);
    }
}

// 🚀 CPU 소모 없이 /dev/input 노드 변경 이벤트를 대기하는 함수
void wait_for_input_change() {
    int inotify_fd = inotify_init();
    if (inotify_fd < 0) {
        sleep(2); // inotify 실패 시 백업용 sleep
        return;
    }

    int wd = inotify_add_watch(inotify_fd, "/dev/input", IN_CREATE | IN_ATTRIB);
    if (wd >= 0) {
        char buf[512];
        // 새로운 /dev/input/eventX 파일이 들어올 때까지 여기서 대기 (CPU 점유율 0%)
        read(inotify_fd, buf, sizeof(buf));
        inotify_rm_watch(inotify_fd, wd);
    }
    close(inotify_fd);
}

int setup_uinput() {
    const char *uinput_paths[] = {"/dev/uinput", "/dev/misc/uinput"};
    for (int i = 0; i < 2; i++) {
        fd_uin = open(uinput_paths[i], O_WRONLY | O_NONBLOCK);
        if (fd_uin >= 0) break;
    }
    if (fd_uin < 0) return -1;

    char dev_name[UINPUT_MAX_NAME_SIZE] = "NVTCapacitivePen";
    struct input_id dev_id = { .bustype = BUS_USB, .vendor = 0x17ef, .product = 0x61a1, .version = 1 };

    ioctl(fd_uin, UI_SET_EVBIT, EV_KEY);
    ioctl(fd_uin, UI_SET_EVBIT, EV_ABS);
    ioctl(fd_uin, UI_SET_EVBIT, EV_SYN);
    ioctl(fd_uin, UI_SET_PROPBIT, INPUT_PROP_DIRECT);

    struct uinput_user_dev uidev;
    memset(&uidev, 0, sizeof(uidev));
    strncpy(uidev.name, dev_name, UINPUT_MAX_NAME_SIZE - 1);
    uidev.id = dev_id;

    uint8_t absbit[(ABS_MAX / 8) + 1] = {0};
    ioctl(fd_pen, EVIOCGBIT(EV_ABS, sizeof(absbit)), absbit);
    for (int axis = 0; axis < ABS_CNT; axis++) {
        if (absbit[axis / 8] & (1 << (axis % 8))) {
            struct input_absinfo absinfo;
            if (ioctl(fd_pen, EVIOCGABS(axis), &absinfo) == 0) {
                ioctl(fd_uin, UI_SET_ABSBIT, axis);
                uidev.absmin[axis] = absinfo.minimum;
                uidev.absmax[axis] = absinfo.maximum;
                uidev.absfuzz[axis] = absinfo.fuzz;
                uidev.absflat[axis] = absinfo.flat;
            }
        }
    }

    uint8_t keybit[(KEY_MAX / 8) + 1] = {0};
    ioctl(fd_pen, EVIOCGBIT(EV_KEY, sizeof(keybit)), keybit);
    for (int key = 0; key < KEY_MAX; key++) {
        if (keybit[key / 8] & (1 << (key % 8))) ioctl(fd_uin, UI_SET_KEYBIT, key);
    }
    ioctl(fd_uin, UI_SET_KEYBIT, BTN_TOUCH);
    ioctl(fd_uin, UI_SET_KEYBIT, BTN_TOOL_PEN);
    ioctl(fd_uin, UI_SET_KEYBIT, BTN_STYLUS);
    ioctl(fd_uin, UI_SET_KEYBIT, BTN_STYLUS2);

    if (write(fd_uin, &uidev, sizeof(uidev)) < 0 || ioctl(fd_uin, UI_DEV_CREATE) < 0) return -1;
    return 0;
}

int main() {
    signal(SIGINT, cleanup);
    signal(SIGTERM, cleanup);

    printf("[+] inotify 기반 초저전력 자동 재연결 데몬 시작\n");

    while (1) {
        // 1. 장치 검색 시도
        close_session();
        fd_pen = find_pen_device(PEN_NAME_KEYWORD);
        find_remote_devices(REMOTE_NAME_KEYWORD);

        // 2. 장치가 둘 다 발견되지 않았으면 이벤트 발생 시까지 대기
        if (fd_pen < 0 || remote_count == 0) {
            wait_for_input_change(); // 🚀 파일 생성 이벤트가 올 때까지 무한 대기 (CPU 0%)
            continue;
        }

        // 3. 가상 장치 생성 및 독점 가로채기
        if (setup_uinput() < 0 || ioctl(fd_pen, EVIOCGRAB, 1) < 0) {
            wait_for_input_change();
            continue;
        }

        for (int i = 0; i < remote_count; i++) {
            ioctl(fd_remotes[i], EVIOCGRAB, 1);
        }

        printf("[+] 블루투스 기기 연결 성공 및 가로채기 완료.\n");

        // 4. 이벤트 처리 루프
        struct pollfd fds[MAX_REMOTES + 1];
        int pen_idx = remote_count;
        int stylus2_pressed = 0, stylus_pressed = 0;
        int disconnected = 0;

        while (!disconnected) {
            for (int i = 0; i < remote_count; i++) {
                fds[i].fd = fd_remotes[i];
                fds[i].events = POLLIN;
            }
            fds[pen_idx].fd = fd_pen;
            fds[pen_idx].events = POLLIN;

            int ret = poll(fds, remote_count + 1, -1);
            if (ret < 0) break;

            for (int i = 0; i < remote_count; i++) {
                if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) { disconnected = 1; break; }
                if (fds[i].revents & POLLIN) {
                    struct input_event ev;
                    ssize_t n = read(fds[i].fd, &ev, sizeof(ev));
                    if (n <= 0) { disconnected = 1; break; }

                    if (ev.type == EV_MSC && ev.code == MSC_SCAN) {
                        if (ev.value == 0x000c0601) {
                            stylus2_pressed = 0;
                            if (stylus_pressed) {
                                stylus_pressed = 0;
                                struct input_event btn_s_up = { .type = EV_KEY, .code = BTN_STYLUS, .value = 0 };
                                struct input_event syn_s    = { .type = EV_SYN, .code = SYN_REPORT, .value = 0 };
                                gettimeofday(&btn_s_up.time, NULL); gettimeofday(&syn_s.time, NULL);
                                write(fd_uin, &btn_s_up, sizeof(btn_s_up)); write(fd_uin, &syn_s, sizeof(syn_s));
                            }
                            struct input_event btn_down = { .type = EV_KEY, .code = BTN_STYLUS2, .value = 1 };
                            struct input_event syn1     = { .type = EV_SYN, .code = SYN_REPORT,  .value = 0 };
                            gettimeofday(&btn_down.time, NULL); gettimeofday(&syn1.time, NULL);
                            write(fd_uin, &btn_down, sizeof(btn_down)); write(fd_uin, &syn1, sizeof(syn1));

                            usleep(HUMAN_CLICK_DELAY_US);

                            struct input_event btn_up   = { .type = EV_KEY, .code = BTN_STYLUS2, .value = 0 };
                            struct input_event syn2     = { .type = EV_SYN, .code = SYN_REPORT,  .value = 0 };
                            gettimeofday(&btn_up.time, NULL); gettimeofday(&syn2.time, NULL);
                            write(fd_uin, &btn_up, sizeof(btn_up)); write(fd_uin, &syn2, sizeof(syn2));
                        }
                        else if (ev.value == 0x000c0612) {
                            stylus2_pressed = 1;
                            struct input_event btn_down = { .type = EV_KEY, .code = BTN_STYLUS2, .value = 1 };
                            struct input_event syn      = { .type = EV_SYN, .code = SYN_REPORT,  .value = 0 };
                            gettimeofday(&btn_down.time, NULL); gettimeofday(&syn.time, NULL);
                            write(fd_uin, &btn_down, sizeof(btn_down)); write(fd_uin, &syn, sizeof(syn));
                        }
                        else if (ev.value == 0x000c0613) {
                            stylus_pressed = 1;
                            struct input_event btn_down = { .type = EV_KEY, .code = BTN_STYLUS, .value = 1 };
                            struct input_event syn      = { .type = EV_SYN, .code = SYN_REPORT, .value = 0 };
                            gettimeofday(&btn_down.time, NULL); gettimeofday(&syn.time, NULL);
                            write(fd_uin, &btn_down, sizeof(btn_down)); write(fd_uin, &syn, sizeof(syn));
                        }
                    }
                }
            }

            if (!disconnected && (fds[pen_idx].revents & (POLLERR | POLLHUP | POLLNVAL))) disconnected = 1;
            if (!disconnected && (fds[pen_idx].revents & POLLIN)) {
                struct input_event ev;
                ssize_t n = read(fd_pen, &ev, sizeof(ev));
                if (n <= 0) { disconnected = 1; continue; }

                if (ev.type == EV_KEY && ev.code == BTN_STYLUS2) {
                    if (!stylus2_pressed) write(fd_uin, &ev, sizeof(ev));
                    continue;
                }
                if (ev.type == EV_KEY && ev.code == BTN_STYLUS) {
                    if (!stylus_pressed) write(fd_uin, &ev, sizeof(ev));
                    continue;
                }
                if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
                    if (stylus2_pressed) {
                        struct input_event btn_ev = { .type = EV_KEY, .code = BTN_STYLUS2, .value = 1 };
                        gettimeofday(&btn_ev.time, NULL); write(fd_uin, &btn_ev, sizeof(btn_ev));
                    }
                    if (stylus_pressed) {
                        struct input_event btn_ev = { .type = EV_KEY, .code = BTN_STYLUS, .value = 1 };
                        gettimeofday(&btn_ev.time, NULL); write(fd_uin, &btn_ev, sizeof(btn_ev));
                    }
                }
                write(fd_uin, &ev, sizeof(ev));
            }
        }

        printf("[!] 블루투스 연결 해제 감지. 신규 장치 연결 대기 중...\n");
    }

    return 0;
}