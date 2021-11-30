# usbkbd_driver_model
userspace usbkbd driver simulator

It is a user space simulator that closely models the callbacks that are defined as the URB completion handlers (usb_kbd_irq and usb_kbd_led) as well as the callback functions that get registered  with  the  Input  subsystem:  usb_kbd_open  and  usb_kbd_event. 

execution instructions: make generates executable named main. main executed using "./main<input.txt"
input.txt file simulates usb keyboard device file in user space.
