# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(priority-queue) begin
(priority-queue) Starting priority queue tests
(priority-queue) Finished test_pqueue_push_pop() successfully
(priority-queue) Finished test_pqueue_remove() successfully
(priority-queue) end
EOF
pass;