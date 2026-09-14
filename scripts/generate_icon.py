#!/usr/bin/env python3
"""
Generate a sample icon for the IMSignalGenerator application.
Creates an icon with a signal waveform theme.
"""

from PIL import Image, ImageDraw, ImageFont
import os
import math

def create_icon():
    # Create a new image with a gradient background
    size = 256
    img = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    # Create gradient background (dark blue to lighter blue)
    for i in range(size):
        color_intensity = int(10 + (i / size) * 40)
        draw.rectangle([(0, i), (size, i+1)],
                      fill=(color_intensity, color_intensity + 30, color_intensity + 60, 255))

    # Draw a border frame
    border_width = 2
    draw.rectangle([(border_width, border_width),
                   (size - border_width, size - border_width)],
                  outline=(100, 150, 200, 255),
                  width=border_width)

    # Draw grid lines (oscilloscope style)
    grid_spacing = size // 8
    for i in range(1, 8):
        x = i * grid_spacing
        y = i * grid_spacing
        # Vertical lines
        draw.line([(x, 10), (x, size - 10)], fill=(50, 70, 100, 128), width=1)
        # Horizontal lines
        draw.line([(10, y), (size - 10, y)], fill=(50, 70, 100, 128), width=1)

    # Draw center lines (stronger)
    center = size // 2
    draw.line([(center, 10), (center, size - 10)], fill=(70, 100, 140, 200), width=2)
    draw.line([(10, center), (size - 10, center)], fill=(70, 100, 140, 200), width=2)

    # Draw a sine wave (main signal)
    wave_points = []
    amplitude = 60
    frequency = 2.5
    y_offset = size // 2

    for x in range(20, size - 20):
        # Calculate sine wave
        angle = (x - 20) / (size - 40) * frequency * 2 * math.pi
        y = y_offset - amplitude * math.sin(angle)
        wave_points.append((x, y))

    # Draw the sine wave with glow effect
    # Draw glow
    for i in range(len(wave_points) - 1):
        draw.line([wave_points[i], wave_points[i + 1]],
                 fill=(100, 200, 255, 100), width=5)

    # Draw main wave
    for i in range(len(wave_points) - 1):
        draw.line([wave_points[i], wave_points[i + 1]],
                 fill=(0, 255, 100, 255), width=3)

    # Draw a second wave (modulated or carrier)
    wave2_points = []
    amplitude2 = 30
    frequency2 = 6

    for x in range(20, size - 20):
        # Calculate modulated wave
        angle = (x - 20) / (size - 40) * frequency2 * 2 * math.pi
        envelope = math.sin((x - 20) / (size - 40) * 1.5 * math.pi) * 0.8 + 0.2
        y = y_offset - amplitude2 * math.sin(angle) * envelope
        wave2_points.append((x, y))

    # Draw the second wave
    for i in range(len(wave2_points) - 1):
        draw.line([wave2_points[i], wave2_points[i + 1]],
                 fill=(255, 150, 0, 200), width=2)

    # Add "SG" text for Signal Generator
    try:
        # Try to use a font, fall back to default if not available
        font = ImageFont.truetype("arial.ttf", 36)
    except:
        font = ImageFont.load_default()

    text = "SG"
    bbox = draw.textbbox((0, 0), text, font=font)
    text_width = bbox[2] - bbox[0]
    text_height = bbox[3] - bbox[1]
    text_x = size - text_width - 15
    text_y = size - text_height - 15

    # Draw text background
    padding = 5
    draw.rounded_rectangle(
        [(text_x - padding, text_y - padding),
         (text_x + text_width + padding, text_y + text_height + padding)],
        radius=5,
        fill=(20, 40, 80, 200)
    )

    # Draw text
    draw.text((text_x, text_y), text, fill=(0, 255, 100, 255), font=font)

    # Add frequency indicator marks at the bottom
    for i in range(5):
        x = 40 + i * 40
        draw.line([(x, size - 25), (x, size - 20)], fill=(150, 200, 255, 255), width=2)

    return img

def main():
    # Create assets directory if it doesn't exist
    assets_dir = os.path.join(os.path.dirname(os.path.dirname(__file__)), 'assets')
    os.makedirs(assets_dir, exist_ok=True)

    # Generate and save icon
    icon = create_icon()
    icon_path = os.path.join(assets_dir, 'icon.png')
    icon.save(icon_path, 'PNG')
    print(f"Icon generated successfully at: {icon_path}")

    # Also create smaller versions for different uses
    for size in [64, 48, 32, 16]:
        small_icon = icon.resize((size, size), Image.Resampling.LANCZOS)
        small_path = os.path.join(assets_dir, f'icon_{size}.png')
        small_icon.save(small_path, 'PNG')
        print(f"Generated {size}x{size} icon at: {small_path}")

if __name__ == "__main__":
    main()
