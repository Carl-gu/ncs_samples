import os

DK_SNR = 0
# DK_SNR = 682334286

if DK_SNR == 0:

    print('Connected device list(snr  port  vcom):')
    print('------------------------------')
    os.system('nrfjprog --com')
    print('------------------------------')

    DK_SNR = input('Input your device serial number(snr): ')

print('device snr is:', DK_SNR)
