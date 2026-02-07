from . import EpaperModel

CONF_CS1_PIN = "cs1_pin"


class T133A01(EpaperModel):
    def __init__(
        self,
        name: str,
        class_name: str = "EPaperT133A01",
        initsequence=(),
        **defaults,
    ):
        super().__init__(name, class_name, initsequence=tuple(initsequence), **defaults)


t133a01 = T133A01(
    "T133A01",
    width=1200,
    height=1600,
    data_rate="20MHz",
    minimum_update_interval="30s",
    reset_duration="20ms",
)

# Pin defaults for Seeed Studio XIAO ePaper Display Board (EE02) + 13.3" six-color panel (T133A01)
# See manufacturer library: EPaper_Board_Pins_Setups.h (USE_XIAO_EPAPER_DISPLAY_BOARD_EE02)
# Note: BUSY is active-low on this board, so we invert it to match epaper_spi busy semantics.

t133a01.extend(
    "Seeed-XIAO-EPaper-13.3in",
    cs_pin=44,
    cs1_pin=41,
    dc_pin=10,
    busy_pin={
        "number": 4,
        "inverted": True,
        "mode": {
            "input": True,
        },
    },
    reset_pin=38,
    enable_pin=[43],
)
