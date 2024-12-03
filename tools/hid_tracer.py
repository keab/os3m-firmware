#!/usr/bin/env python
# -*- coding: utf-8 -*-

#
"""
This tool will receive raw data from the os3m, do some calculations on the data and print results. 
The purpose is a) to easier find the right settings for honing and autozero b) Check that buttons are reported as expected, c) Check that the data looks sane
It will work both with the original firmware (acting as a STM HID device), and the "spoof" firmware acting as a 3dconnexion HID device
The code is based on the pywinusb examples
Only used on Windows 11.
"""
from time import sleep, time
from msvcrt import kbhit
import struct

import pywinusb.hid as hid

prev=[]
autozero_comp = []
autozero_cnt=0
cache_3dc=[]

#Handler for the original os3m fw acting as an STM hid device
def handler_stm(data):
    unpacked = struct.unpack('<B'+'h'*(len(data)//2), bytes(data))
    handler_common(unpacked[1:])

#The spoof firmware sends translation and rotation in different messages, so we need to cache the first message 
def handler_3dc(data):
    global cache_3dc
    
    #print("Raw data: {0}".format(data))
    
    m_id = data[0]
    if m_id == 1:
        #x,y,z
        if cache_3dc:
            print('Debug: Lost a #2 packet! Should never happen.')
        unpacked = struct.unpack('<B'+'h'*(len(data)//2), bytes(data))
        cache_3dc = list(unpacked[1:])
    elif m_id == 2:
        #rx,ry,rz
        if cache_3dc:
            unpacked = struct.unpack('<B'+'h'*(len(data)//2), bytes(data))
            #print(cache_3dc)
            cache_3dc.extend(unpacked[1:])
            #print(data, data[1:],cache_3dc)
            handler_common(cache_3dc)
            cache_3dc=[]
        else:
            print('Debug: Lost a #1 packet! Should never happen.')
    elif m_id == 3:
        #button-bits   
        high_bits=[ i for i in range(48) if data[1 + i//8] & (1 << i%8)]
        
        print("Buttons (bit 0 is the rightmost): {5:08b} {4:08b} {3:08b} {2:08b} {1:08b} {0:08b}    High-bits: {6}".format(data[1],data[2],data[3],data[4],data[5],data[6], high_bits))
    else:
        print("Unexpected/unknown message: {0}".format(data))

#Diffs shall be a list/tuple with all six sensor readings (signed integers, as offset to from the rest position)
def handler_common(diffs):
    global prev
    global autozero_cnt,autozero_comp

    
    if autozero_comp:
        diffs = [a-b for a,b in zip(diffs, autozero_comp)]
    
    #print("Diffs: {0}".format(diffs))
    abs_diffs = [abs(a) for a in diffs]
    total_diff=sum(abs_diffs)
    max_diff = max(abs_diffs)
    
    abs_chng = []
    total_chng = 0
    max_chng = 0
    if(prev):
        abs_chng = [abs(a_i - b_i) for a_i, b_i in zip(diffs, prev)]
        total_chng = sum(abs_chng)
        max_chng = max(abs_chng)
    
    prev=diffs
    #print(diffs, total_diff)
    #print(abs_chng, total_chng)
    td = ''
    tdl=900
    if total_diff < tdl:
        td='DifTot_Lo'
    md=''
    mdl=100
    if max_diff < mdl:
        md='DifMax_Lo'
    
    db=''
    dbl = 20
    if max_diff < dbl:
        db='InDeadb'
    
    tc=''
    tcl=20
    if total_chng < tcl:
        tc='ChngTot_Lo'
        autozero_cnt += 1
    else:
        autozero_cnt = 0
    mc=''
    mcl=10
    if max_chng < mcl:
        mc='ChngMax_Lo'
    
    az=''
    acl=20
    if autozero_cnt >= acl:
        autozero_comp = diffs
        autozero_cnt = 0
        az='Autozero!'
    
    print('{:20} {:6d} {:6d} {:6d} {:6d} {:6d} {:6d} | {:11s}({:4}/{}) | {:11s}({:4}/{}) | {:11s}({:4}/{}) | {:11s}({:4}/{}) | {:11s}({:4}/{}) | {:11s}({:2}/{})  |'.format(time(),*diffs,td,total_diff,tdl,md,max_diff, mdl,db,max_diff, dbl,tc,total_chng,tcl,mc,max_chng,mcl,az,autozero_cnt,acl))
    
def handler_raw(data):
    print("Raw data: {0}".format(data))


    
def os3m_trace():
    
    #This will match the original os3m firmare. Vendor = STM, DeviceId=??
    device_filter_stm = hid.HidDeviceFilter(vendor_id = 0x0483,
                    product_id = 0x572b)
                    
    #This will match the spoof-firmware  Vendor=3DConnexion, DeviceId= "SpaceMouse Pro Wireless (cabled)"
    device_filter_3dc = hid.HidDeviceFilter(vendor_id = 0x256f,
                    product_id = 0xc631)
    
    #Spacemouse pro, as trialed. Vendor= Logitec, DeviceId= "3Dconnexion Space Mouse Pro"
    device_filter_3dc_pro = hid.HidDeviceFilter(vendor_id = 0x046d,
                    product_id = 0xc62b)    
                    
    configs = [(device_filter_3dc, handler_3dc), (device_filter_3dc_pro, handler_3dc),(device_filter_stm, handler_stm)]

    for c in configs:
        all_hids = c[0].get_devices()
        if all_hids:
            #Assume there is only one device of this kind attached, or in any case use only the first one
            device = all_hids[0]
            device_name = unicode("{0.vendor_name} {0.product_name}" \
                        "(vID=0x{1:04x}, pID=0x{2:04x})"\
                        "".format(device, device.vendor_id, device.product_id))
            print("Using device: {0}".format(device_name))
            try:
                device.open()

                #set the associated raw data handler
                device.set_raw_data_handler(c[1])

                print("\nWaiting for data...\nPress any (system keyboard) key to stop...")
                while not kbhit() and device.is_plugged():
                    #just keep the device opened to receive events
                    sleep(0.5)
                    
                return
            finally:
                device.close()
                exit
    
    # None of the expected devices were found, so browse devices... (this is kept from the pywinusb example file)
    print("None of the expected/pre-defined devices were found")
    all_hids = hid.find_all_hid_devices()
    if all_hids:
        while True:
            print("Choose a device to monitor raw input reports:\n")
            print("0 => Exit")
            for index, device in enumerate(all_hids):
                device_name = unicode("{0.vendor_name} {0.product_name}" \
                        "(vID=0x{1:04x}, pID=0x{2:04x})"\
                        "".format(device, device.vendor_id, device.product_id))
                print("{0} => {1}".format(index+1, device_name))
            print("\n\tDevice ('0' to '%d', '0' to exit?) " \
                    "[press enter after number]:" % len(all_hids))
            index_option = raw_input()
            if index_option.isdigit() and int(index_option) <= len(all_hids):
                # invalid
                break;
        int_option = int(index_option)
        if int_option:
            device = all_hids[int_option-1]
            try:
                device.open()

                #set custom raw data handler
                device.set_raw_data_handler(handler_raw)

                print("\nWaiting for data...\nPress any (system keyboard) key to stop...")
                while not kbhit() and device.is_plugged():
                    #just keep the device opened to receive events
                    sleep(0.5)
                    
                return
            finally:
                device.close()
    else:
        print("There's not any non system HID class device available")
#
if __name__ == '__main__':
    # first be kind with local encodings
    import sys
    if sys.version_info >= (3,):
        # as is, don't handle unicodes
        unicode = str
        raw_input = input
    else:
        # allow to show encoded strings
        import codecs
        sys.stdout = codecs.getwriter('mbcs')(sys.stdout)

    os3m_trace()

