#!/usr/bin/env bash

PROJECT_ROOT=$(git rev-parse --show-toplevel)
NUM_PEERS=2
BIG_FILE="$PROJECT_ROOT/test_data/gec2u.flac"
SMALL_FILE="$PROJECT_ROOT/test_data/video.webm"

echo "[*] Building project"
make

echo "[*] Starting tracker"
mkdir -p /tmp/tracker
cp $PROJECT_ROOT/tracker /tmp/tracker
cd /tmp/tracker

./tracker > tracker.out 2>&1 &
TRACKER_PID=$!

PEER_PIDS=()

cleanup() {

    # kill peers
    for pid in "${PEER_PIDS[@]}"; do
        kill "$pid" 2>/dev/null
    done

    # kill tracker
    kill "$TRACKER_PID" 2>/dev/null

    rm -rf "/tmp/tracker"

    for i in {1..13}; do
        rm -rf "/tmp/peer$i"
    done;
}

create_range_of_peers(){
    local start=$1
    local end=$2

    # create the first 2 peers
    for ((i=start; i<=end; i++)); do
        echo "[*] Creating peer $i"
        PEER_DIR="/tmp/peer$i"

        mkdir -p "$PEER_DIR"
        mkdir -p "$PEER_DIR/downloads"

        cp "$PROJECT_ROOT/clientThreadConfig.cfg" "$PEER_DIR"
        cp "$PROJECT_ROOT/peer" "$PEER_DIR"

        # write config file
        cat <<EOF > "$PEER_DIR/serverThreadConfig.cfg"
808$i
downloads
EOF

        mkfifo "$PEER_DIR/peer$i.in"

        cd "$PEER_DIR"
        ./peer < peer$i.in > peer$i.out 2>&1 &
        PEER_PIDS+=($!)

done

}



create_range_of_peers 1 2


echo "[*] Creating big file tracker on peer1"
cp "$BIG_FILE" /tmp/peer1/downloads/
echo "create_tracker $(basename $BIG_FILE) big_file 0 0" >> /tmp/peer1/peer1.in

echo "[*] Creating small file tracker on peer1"
cp "$SMALL_FILE" /tmp/peer2/downloads/
echo "create_tracker $(basename $SMALL_FILE) small_file 0 0" >> /tmp/peer2/peer2.in

sleep 10

create_range_of_peers 3 8
for i in {3..8}; do
    echo "[*] Getting files on peer$i"
    echo "get $(basename $SMALL_FILE)" >> /tmp/peer$i/peer$i.in
    echo "get $(basename $BIG_FILE)" >> /tmp/peer$i/peer$i.in
done;

sleep 60


create_range_of_peers 9 13
for i in {9..13}; do
    echo "[*] Getting files on peer$i"
    echo "get $(basename $SMALL_FILE)" >> /tmp/peer$i/peer$i.in
    echo "get $(basename $BIG_FILE)" >> /tmp/peer$i/peer$i.in
done;

sleep 60

read -p "Press Enter to cleanup..."
cleanup
