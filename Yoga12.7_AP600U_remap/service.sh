#!/system/bin/sh
MODDIR=${0%/*}

# 실행할 데몬 파일 리스트
DAEMONS="ap500u ap600u"

for daemon in $DAEMONS; do
    TARGET="$MODDIR/$daemon"
    
    # 파일이 존재하는지 확인 후 권한 변경 및 백그라운드 실행
    if [ -f "$TARGET" ]; then
        chmod 777 "$TARGET"
        "$TARGET" &
    fi
done