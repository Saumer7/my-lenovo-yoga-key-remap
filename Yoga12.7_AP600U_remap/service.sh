#!/system/bin/sh
MODDIR=${0%/*}

# 모듈 폴더 내에 있는 keyboard 파일의 절대 경로
TARGET="$MODDIR/ap600u"

# 파일이 존재하는지 확인 후 권한 변경 및 실행
if [ -f "$TARGET" ]; then
    chmod 777 "$TARGET"
    "$TARGET" &
fi