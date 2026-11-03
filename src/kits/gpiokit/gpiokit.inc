; gpiokit.inc - constantes publiques de GpioKit (gpiokit.abi, docs/KITS.md)

GP_PINS		= 28		; GPIO 0..27 : ceux du connecteur

; modes (gp_mode)
GP_M_FREE	= 0		; a personne : entree sans tirage (gp_mode rend la broche)
GP_M_IN		= 1		; entree flottante
GP_M_PULLUP	= 2		; entree tiree vers le haut
GP_M_PULLDOWN	= 3		; entree tiree vers le bas
GP_M_OUT	= 4		; sortie (commence basse)
GP_M_PWM	= 5		; sortie PWM (gp_pwm la met)
GP_M_I2C	= 6		; broche du bus I2C (gp_i2c_open)
GP_M_SPI	= 7		; broche de SPI 0 (gp_spi_open)
GP_M_ALT0	= 8		; GP_M_ALT0 + n : fonction alternative n (0..5) a la main

GP_RISING	= 1		; gp_edges : les fronts voulus
GP_FALLING	= 2
GP_BOTH		= 3

; codes d'erreur (gp_error)
GP_EPERM	= -1		; broche du systeme, ou pas au programme
GP_EIO		= -5		; pas de reponse sur le bus
GP_ENOMEM	= -12
GP_EFAULT	= -14
GP_EBUSY	= -16		; un autre programme la tient
GP_ENODEV	= -19		; pas de GPIO ici : gp_sim (1) simule
GP_EINVAL	= -22		; broche, mode, frequence, rapport faux

; drapeaux de GPI_FLAGS
GP_F_RESERVED	= 1		; au systeme : jamais donnee (GPI_REASON dit a quoi)
GP_F_PWM	= 2		; peut faire du PWM (12, 13, 18, 19)
GP_F_EDGES	= 4		; ses fronts vont a son proprietaire

; une broche vue du systeme (gp_info)
GPI_PIN		= 0		; octet : numero de GPIO
GPI_MODE	= 1		; octet
GPI_LEVEL	= 2		; octet
GPI_FLAGS	= 3		; octet
GPI_OWNER	= 4		; pid du proprietaire, 0 : personne
GPI_FREQ	= 8		; Hz, en PWM
GPI_DUTY	= 12		; 1/10000, en PWM
GPI_REASON	= 16		; 24 o : pourquoi elle est reservee
GPI_SIZE	= 40

; un front (gp_events)
GPE_PIN		= 0		; octet
GPE_EDGE	= 1		; octet GP_RISING / GP_FALLING
GPE_LEVEL	= 2		; octet : le niveau juste apres
GPE_LOST	= 3		; octet : 1 des fronts ont ete perdus avant celui-ci
GPE_US		= 8		; quand, en microsecondes (horloge de gp_now_us)
GPE_SIZE	= 16
