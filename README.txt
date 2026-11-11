Group Information: 
    Devon Weckherlen - ID: 702757732
        - Contribution:
    Achilles Demerutis - ID: 702753287
        - Contribution:

MILESTONE 1:
Client requests file -> Server sends file over UDP with NO LOSS.

MILESTONE 2:
Add ACK support.

MILESTONE 3:
Add sliding window.

MILESTONE 4:
Add timeouts and retransmissions.

MILESTONE 5:
Add reliable EOT.

MILESTONE 6:
Run loss tests and verify statistics.


==================================================
CISC 450 - Programming Project 2 Development Notes
==================================================

GOAL
----
Convert Project 1's TCP file transfer into a UDP-based file transfer
that implements Go-Back-N (GBN) reliability.

Server = GBN Sender
Client = GBN Receiver

=======================================================

Road Map:

    Step 1: Make all necessary files --> COMPLETE
        packet.c
        packet.h
        server.c
        client.c
        lossy.c
        lossy.h
        Makefile
        README.txt

    Step 2: Update packet.c and packet.h
        Define packet types:
            REQUEST
            DATA
            ACK
            EOT

        Define MAX_DATA = 80

        Implement packet serialization
        Implement packet deserialization
        Convert fields using htons() and ntohs()
        Verify packet length and count values

        This is the foundation for the entire project.

    Step 3: Build the UDP server skeleton
        Create UDP socket
        Bind to specified port
        Wait for REQUEST packet
        Store client address information
        Close socket
        Exit

    Step 4: Build the UDP client skeleton
        Create UDP socket
        Prompt user for filename
        Send REQUEST packet
        Wait for server response
        Close socket
        Exit

    Step 5: Implement REQUEST packet handling
        Client:
            Prompt user for filename
            Create REQUEST packet
            Send filename to server

        Server:
            Receive REQUEST packet
            Extract filename
            Open requested file
            Store client address

        This is the first successful client-server exchange.

    Step 6: Implement DATA packet transfer (no GBN yet)
        Server:
            Read file line by line
            Create DATA packets
            Assign sequence numbers starting at 1
            Send packets to client

        Client:
            Receive DATA packets
            Write data to out.txt

        Focus on getting a complete transfer working
        before adding reliability.

    Step 7: Implement ACK packets
        Client:
            Send ACK for received DATA packet

        Server:
            Receive ACK packets
            Print required ACK messages

        Verify ACKs are being exchanged correctly.

    Step 8: Implement Go-Back-N sender logic
        Add:
            base
            next_seq
            window_size

        Send packets while window has space
        Slide window forward when ACKs arrive
        Ignore duplicate and stale ACKs

        This is the core of the assignment.

    Step 9: Implement timer and retransmissions
        Start timer when first packet enters window
        Stop timer when window becomes empty

        On timeout:
            Retransmit all outstanding packets
            Restart timer
            Update timeout statistics

    Step 10: Implement receiver logic
        Maintain expected_seq

        If correct packet arrives:
            Accept packet
            Write packet data
            Send ACK

        If duplicate or out-of-order packet arrives:
            Discard packet
            Resend previous ACK

    Step 11: Implement EOT handling
        Server:
            Send EOT packet after all DATA packets
            Retransmit EOT if necessary
            Wait for EOT ACK

        Client:
            Receive EOT
            Send EOT ACK
            Enter linger state

    Step 12: Implement statistics
        Server:
            Original packets
            Retransmissions
            Original bytes
            Timeouts

        Client:
            Accepted packets
            Discarded packets
            Bytes written

    Step 13: Testing
        Test:
            Empty file
            Single-line file
            Multiple-line file
            Blank lines
            Missing final newline

        Test:
            Small window sizes
            Large files
            Packet loss
            ACK loss
            EOT loss

        Verify:
            out.txt exactly matches original file

    Step 14: Final cleanup
        Make sure all required console output matches spec
        Remove warnings
        Verify Makefile works
        Finish README
        Verify files compile on course system
        Create submission zip

=======================================================

./server 9000 4 1000 0.0 1
./client 127.0.0.1 9000 1000 0.0 1