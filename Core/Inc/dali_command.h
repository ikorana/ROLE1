#ifndef DALI_COMMAND_H_
#define DALI_COMMAND_H_

#define L_SET_MAX_LEVEL             0x2A      
#define L_SET_MIN_LEVEL             0x2B
#define L_SET_SYS_ERR_LEVEL         0x2C
#define L_SET_POWER_ON_LEVEL        0x2D
#define L_SET_FADE_TIME             0x2E
#define L_SET_FADE_RATE             0x2F
#define L_SET_EFADE_TIME            0x30

#define L_ADD_SCENE                 0x40
#define L_REMOVE_SCENE              0x50
#define L_ADD_GROUP                 0x60
#define L_REMOVE_GROUP              0x70
#define L_SET_SHORT_ADDR            0x80 

#define L_QUERY_MISSING_SHORT_ADR   0x96
#define L_QUERY_CONTENT_DTR0        0x98 
#define L_QUERY_DEV_TYPE            0x99
#define L_QUERY_PHY_MIN             0x9A
#define L_QUERY_POWER_FAILURE       0x9B
#define L_QUERY_MAX_LEVEL           0xA1
#define L_QUERY_MIN_LEVEL           0xA2
#define L_QUERY_POWER_ON_LEVEL      0xA3
#define L_QUERY_SYS_FAIL_LEVEL      0xA4
#define L_QUERY_TIME_RATE           0xA5
#define L_QUERY_NEXT_DEV_TYPE       0xA7
#define L_QUERY_EFADE_TIME          0xA8

#define L_QUERY_SCENE               0xB0
#define L_QUERY_GROUP0              0xC0
#define L_QUERY_GROUP1              0xC1


#define L_QUERY_RAND_ADR_H          0xC2
#define L_QUERY_RAND_ADR_M          0xC3
#define L_QUERY_RAND_ADR_L          0xC4
#define L_SAVE_PERS_VAR             0x22



#define DALI_TERMINATE          0xA1
#define DALI_STORE_DTR          0xA3
#define DALI_INITIALISE         0xA5
#define DALI_RANDOMISE          0xA7
#define DALI_COMPARE            0xA9
#define DALI_WITHDRAW           0xAB
#define DALI_PING               0xAD
#define DALI_SEARCHADDRH        0xB1
#define DALI_SEARCHADDRM        0xB3
#define DALI_SEARCHADDRL        0xB5
#define DALI_PROG_SHORT_ADDR    0xB7
#define DALI_VERIFY_SHORT_ADDR  0xB9
#define DALI_QUERY_SHORT_ADDR   0xBB


#endif