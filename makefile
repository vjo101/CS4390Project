CC = gcc
CFLAGS = -Wall

PEER_DIRS = peer1_dir peer2_dir peer3_dir
TARGETS = peer1_dir/peer1 peer2_dir/peer2 peer3_dir/peer3

all: tracker $(TARGETS)

tracker: tracker.o
	$(CC) $(CFLAGS) -o $@ $<

tracker.o: tracker.c
	$(CC) $(CFLAGS) -c $< -o $@

$(PEER_DIRS):
	echo '$(PEER_DIRS)'
	mkdir -p $@

peer.o: peer.c
	$(CC) $(CFLAGS) -c $< 

$(TARGETS): peer.o $(PEER_DIRS)
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -rf $(PEER_DIRS)
