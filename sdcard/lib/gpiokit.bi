# gpiokit.bi -- gpiokit pour le BASIC (#import gpiokit) : genere par tools/kits/abi2bas.py depuis gpiokit.abi ; ne pas editer.
kit gpiokit 34
available 0 l - gp_available
edges 1 l ss gp_edges a0,a1
error 2 s s gp_error a0
events 3 l sss gp_events a0,a1,a2
gpio_function 4 s s gp_gpio_function a0
header_gpio 5 l s gp_header_gpio a0
header_label 6 s s gp_header_label a0
header_pin 7 l s gp_header_pin a0
i2c_close 8 l - gp_i2c_close
i2c_guess 9 s s gp_i2c_guess a0
i2c_open 10 l s gp_i2c_open a0
i2c_read 11 l sss gp_i2c_read a0,a1,a2
i2c_reg_read 12 l ss gp_i2c_reg_read a0,a1
i2c_reg_write 13 l sss gp_i2c_reg_write a0,a1,a2
i2c_scan 14 l s gp_i2c_scan a0
i2c_write 15 l sss gp_i2c_write a0,a1,a2
i2c_write_read 16 l sssss gp_i2c_write_read a0,a1,a2,a3,a4
info 17 l ss gp_info a0,a1
mode 18 l ss gp_mode a0,a1
mode_name 19 s s gp_mode_name a0
now_us 20 l - gp_now_us
pwm 21 l sss gp_pwm a0,a1,a2
read 22 l s gp_read a0
read_all 23 l - gp_read_all
release 24 l - gp_release
servo 25 l ss gp_servo a0,a1
sim 26 l s gp_sim a0
sim_display 27 l s gp_sim_display a0
sim_input 28 l ss gp_sim_input a0,a1
spi_close 29 l - gp_spi_close
spi_open 30 l ss gp_spi_open a0,a1
spi_transfer 31 l ssss gp_spi_transfer a0,a1,a2,a3
toggle 32 l s gp_toggle a0
write 33 l ss gp_write a0,a1
