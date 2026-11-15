$(eval $(call module,w202ds-touchscreen-himax,touchscreen/source/himax,himax_w202.ko,w202ds-drm-panel))
.PHONY: w202ds-touchscreen-driver
w202ds-touchscreen-driver: w202ds-touchscreen-himax
# Touch input is shared board support for desktop profiles.
SERVER_MODULES += w202ds-touchscreen-himax
