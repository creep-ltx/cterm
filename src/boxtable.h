/* Box drawing U+2500-257F: the four arms of each character, two bits
 * each - up<<6 | right<<4 | down<<2 | left; 0 none, 1 light, 2 heavy,
 * 3 double. 0: a diagonal (U+2571-2573, drawn by code). Made from Unicode's names (unicodedata). */
static const unsigned char boxarms[128] = {
    0x11, /* 2500 light horizontal */
    0x22, /* 2501 heavy horizontal */
    0x44, /* 2502 light vertical */
    0x88, /* 2503 heavy vertical */
    0x11, /* 2504 light triple dash horizontal */
    0x22, /* 2505 heavy triple dash horizontal */
    0x44, /* 2506 light triple dash vertical */
    0x88, /* 2507 heavy triple dash vertical */
    0x11, /* 2508 light quadruple dash horizontal */
    0x22, /* 2509 heavy quadruple dash horizontal */
    0x44, /* 250A light quadruple dash vertical */
    0x88, /* 250B heavy quadruple dash vertical */
    0x14, /* 250C light down and right */
    0x24, /* 250D down light and right heavy */
    0x18, /* 250E down heavy and right light */
    0x28, /* 250F heavy down and right */
    0x05, /* 2510 light down and left */
    0x06, /* 2511 down light and left heavy */
    0x09, /* 2512 down heavy and left light */
    0x0A, /* 2513 heavy down and left */
    0x50, /* 2514 light up and right */
    0x60, /* 2515 up light and right heavy */
    0x90, /* 2516 up heavy and right light */
    0xA0, /* 2517 heavy up and right */
    0x41, /* 2518 light up and left */
    0x42, /* 2519 up light and left heavy */
    0x81, /* 251A up heavy and left light */
    0x82, /* 251B heavy up and left */
    0x54, /* 251C light vertical and right */
    0x64, /* 251D vertical light and right heavy */
    0x94, /* 251E up heavy and right down light */
    0x58, /* 251F down heavy and right up light */
    0x98, /* 2520 vertical heavy and right light */
    0xA4, /* 2521 down light and right up heavy */
    0x68, /* 2522 up light and right down heavy */
    0xA8, /* 2523 heavy vertical and right */
    0x45, /* 2524 light vertical and left */
    0x46, /* 2525 vertical light and left heavy */
    0x85, /* 2526 up heavy and left down light */
    0x49, /* 2527 down heavy and left up light */
    0x89, /* 2528 vertical heavy and left light */
    0x86, /* 2529 down light and left up heavy */
    0x4A, /* 252A up light and left down heavy */
    0x8A, /* 252B heavy vertical and left */
    0x15, /* 252C light down and horizontal */
    0x16, /* 252D left heavy and right down light */
    0x25, /* 252E right heavy and left down light */
    0x26, /* 252F down light and horizontal heavy */
    0x19, /* 2530 down heavy and horizontal light */
    0x1A, /* 2531 right light and left down heavy */
    0x29, /* 2532 left light and right down heavy */
    0x2A, /* 2533 heavy down and horizontal */
    0x51, /* 2534 light up and horizontal */
    0x52, /* 2535 left heavy and right up light */
    0x61, /* 2536 right heavy and left up light */
    0x62, /* 2537 up light and horizontal heavy */
    0x91, /* 2538 up heavy and horizontal light */
    0x92, /* 2539 right light and left up heavy */
    0xA1, /* 253A left light and right up heavy */
    0xA2, /* 253B heavy up and horizontal */
    0x55, /* 253C light vertical and horizontal */
    0x56, /* 253D left heavy and right vertical light */
    0x65, /* 253E right heavy and left vertical light */
    0x66, /* 253F vertical light and horizontal heavy */
    0x95, /* 2540 up heavy and down horizontal light */
    0x59, /* 2541 down heavy and up horizontal light */
    0x99, /* 2542 vertical heavy and horizontal light */
    0x96, /* 2543 left up heavy and right down light */
    0xA5, /* 2544 right up heavy and left down light */
    0x5A, /* 2545 left down heavy and right up light */
    0x69, /* 2546 right down heavy and left up light */
    0xA6, /* 2547 down light and up horizontal heavy */
    0x6A, /* 2548 up light and down horizontal heavy */
    0x9A, /* 2549 right light and left vertical heavy */
    0xA9, /* 254A left light and right vertical heavy */
    0xAA, /* 254B heavy vertical and horizontal */
    0x11, /* 254C light double dash horizontal */
    0x22, /* 254D heavy double dash horizontal */
    0x44, /* 254E light double dash vertical */
    0x88, /* 254F heavy double dash vertical */
    0x33, /* 2550 double horizontal */
    0xCC, /* 2551 double vertical */
    0x34, /* 2552 down single and right double */
    0x1C, /* 2553 down double and right single */
    0x3C, /* 2554 double down and right */
    0x07, /* 2555 down single and left double */
    0x0D, /* 2556 down double and left single */
    0x0F, /* 2557 double down and left */
    0x70, /* 2558 up single and right double */
    0xD0, /* 2559 up double and right single */
    0xF0, /* 255A double up and right */
    0x43, /* 255B up single and left double */
    0xC1, /* 255C up double and left single */
    0xC3, /* 255D double up and left */
    0x74, /* 255E vertical single and right double */
    0xDC, /* 255F vertical double and right single */
    0xFC, /* 2560 double vertical and right */
    0x47, /* 2561 vertical single and left double */
    0xCD, /* 2562 vertical double and left single */
    0xCF, /* 2563 double vertical and left */
    0x37, /* 2564 down single and horizontal double */
    0x1D, /* 2565 down double and horizontal single */
    0x3F, /* 2566 double down and horizontal */
    0x73, /* 2567 up single and horizontal double */
    0xD1, /* 2568 up double and horizontal single */
    0xF3, /* 2569 double up and horizontal */
    0x77, /* 256A vertical single and horizontal double */
    0xDD, /* 256B vertical double and horizontal single */
    0xFF, /* 256C double vertical and horizontal */
    0x14, /* 256D light arc down and right */
    0x05, /* 256E light arc down and left */
    0x41, /* 256F light arc up and left */
    0x50, /* 2570 light arc up and right */
    0x00, /* 2571 light diagonal upper right to lower left */
    0x00, /* 2572 light diagonal upper left to lower right */
    0x00, /* 2573 light diagonal cross */
    0x01, /* 2574 light left */
    0x40, /* 2575 light up */
    0x10, /* 2576 light right */
    0x04, /* 2577 light down */
    0x02, /* 2578 heavy left */
    0x80, /* 2579 heavy up */
    0x20, /* 257A heavy right */
    0x08, /* 257B heavy down */
    0x21, /* 257C light left and heavy right */
    0x48, /* 257D light up and heavy down */
    0x12, /* 257E heavy left and light right */
    0x84, /* 257F heavy up and light down */
};
