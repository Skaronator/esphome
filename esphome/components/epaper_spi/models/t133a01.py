from . import EpaperModel


class T133A01Model(EpaperModel):
    def __init__(self, name, **defaults):
        super().__init__(name, "EPaperT133A01", **defaults)

    def get_init_sequence(self, config: dict):
        """
        Generate init sequence for T133A01 display based on t133a01.md specifications.
        """
        width, height = self.get_dimensions(config)

        # T133A01 initialization sequence
        return (
            (0x74, 0xF0),  # Set Power Saving
            (0x00, 0x5F, 0x69),  # Panel Setting Register
            (0x50, 0x3F),  # CDI Setting
            (0x60, 0x02, 0x00),  # Undocumented
            (0x86, 0x01),  # Undocumented
            (0xE3, 0x2F),  # Power Settings
            (0x61, width >> 8, width & 0xFF, height >> 8, height & 0xFF),  # Resolution
            (0x01, 0x3F),  # Power Setting
            (0xB6, 0x0D),  # Booster Positive Voltage
            (0xB7, 0x0D),  # Booster Negative Voltage
            (0xB0, 0x00),  # Undocumented
            (0xB1, 0x00),  # Undocumented
        )

    def get_constructor_args(self, config) -> tuple:
        return ()


# Create the base T133A01 model with default parameters
t133a01_base = T133A01Model(
    "t133a01",
    width=1200,
    height=1600,
    data_rate="10MHz",
)

# Extended model for XIAO ePaper Display Board EE02
t133a01_base.extend(
    "xiao-epaper-13.3in-ee02",
    width=1200,
    height=1600,
    data_rate="10MHz",
)
