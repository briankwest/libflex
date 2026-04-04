#!/bin/bash
# level_sweep.sh — Interactive FLEX TX level sweep
# Transmits a page at each level and asks if the pager decoded it.
# Usage: ./level_sweep.sh [options]
#   -c  capcode (default 1234567)
#   -s  speed: 1600, 3200, 3200-4, 6400 (default 1600)
#   -P  PTT device (default /dev/hidraw1)
#   -D  audio device (default hw:1,0)
#   -d  TX delay ms (default 1500)
#   -m  message text (default "sweep test")

LIBPATH="/home/brian/libflex/src/.libs"
BINDIR="/home/brian/libflex/examples/.libs"

CAPCODE=1234567
SPEED=1600
HIDRAW="/dev/hidraw1"
AUDIO="hw:1,0"
DELAY=1500
MSG="sweep test"

while getopts "c:s:P:D:d:m:" opt; do
    case $opt in
        c) CAPCODE="$OPTARG" ;;
        s) SPEED="$OPTARG" ;;
        P) HIDRAW="$OPTARG" ;;
        D) AUDIO="$OPTARG" ;;
        d) DELAY="$OPTARG" ;;
        m) MSG="$OPTARG" ;;
    esac
done

TX() {
    local level="$1"
    sudo LD_LIBRARY_PATH="$LIBPATH" "$BINDIR/flex_tx" \
        -c "$CAPCODE" -m "${MSG} l${level}" -s "$SPEED" \
        -l "$level" -P "$HIDRAW" -D "$AUDIO" -d "$DELAY" 2>/dev/null
}

echo "========================================="
echo " FLEX Level Sweep"
echo "========================================="
echo " Capcode: $CAPCODE"
echo " Speed:   $SPEED"
echo " PTT:     $HIDRAW"
echo " Audio:   $AUDIO"
echo " Delay:   ${DELAY}ms"
echo ""
echo " Commands at prompt:"
echo "   y/n    — decoded or not"
echo "   r      — repeat same level"
echo "   NUMBER — jump to that level (e.g. 0.07)"
echo "   s MODE — change speed (1600/3200/3200-4/6400)"
echo "   d NUM  — set TX delay"
echo "   q      — quit"
echo "========================================="
echo ""

LEVELS=(0.01 0.02 0.03 0.04 0.05 0.06 0.07 0.08 0.10 0.12 0.15 0.20 0.25 0.30 0.40 0.50)
RESULTS=()
IDX=0

while true; do
    if [ $IDX -ge ${#LEVELS[@]} ]; then
        echo ""
        echo "=== Sweep complete ==="
        break
    fi

    LVL="${LEVELS[$IDX]}"
    echo -n "TX level $LVL (${SPEED} bps) ... "
    TX "$LVL"
    echo "sent."

    while true; do
        read -p "  Decoded? [y/n/r/NUMBER/s MODE/d NUM/q]: " ans
        case "$ans" in
            y|Y)
                echo "  >>> $LVL DECODED <<<"
                RESULTS+=("$LVL@${SPEED}:YES")
                IDX=$((IDX + 1))
                break
                ;;
            n|N)
                RESULTS+=("$LVL@${SPEED}:NO")
                IDX=$((IDX + 1))
                break
                ;;
            r|R)
                echo -n "  Repeating $LVL ... "
                TX "$LVL"
                echo "sent."
                ;;
            s\ *)
                SPEED="${ans#s }"
                echo "  Speed set to $SPEED"
                echo -n "  Repeating $LVL ... "
                TX "$LVL"
                echo "sent."
                ;;
            d\ *)
                DELAY="${ans#d }"
                echo "  TX delay set to ${DELAY}ms"
                echo -n "  Repeating $LVL ... "
                TX "$LVL"
                echo "sent."
                ;;
            q|Q)
                break 2
                ;;
            [0-9]*)
                LVL="$ans"
                LEVELS[$IDX]="$LVL"
                echo -n "  TX level $LVL ... "
                TX "$LVL"
                echo "sent."
                ;;
            *)
                echo "  y/n/r/NUMBER/s MODE/d NUM/q"
                ;;
        esac
    done
done

echo ""
echo "========================================="
echo " Results"
echo "========================================="
for r in "${RESULTS[@]}"; do
    key="${r%%:*}"
    res="${r##*:}"
    if [ "$res" = "YES" ]; then
        printf "  %-14s  DECODED\n" "$key"
    else
        printf "  %-14s  no decode\n" "$key"
    fi
done
echo "========================================="
