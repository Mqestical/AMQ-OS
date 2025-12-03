#!/usr/bin/env python3
import os
import re

# Color mapping dictionary (RGB part only)
COLOR_MAP = {
    '000000': 'BLACK',
    'ffffff': 'WHITE',
    'ff0000': 'RED',
    '00ff00': 'GREEN',
    '0000ff': 'BLUE',
    'ffff00': 'YELLOW',
    '00ffff': 'CYAN',
    'ff00ff': 'MAGENTA',
    '808080': 'GRAY',
    'ffa500': 'ORANGE',
    '800080': 'PURPLE',
    'a52a2a': 'BROWN',
    'ffc0cb': 'PINK',
}

def replace_colors_in_file(filepath):
    """Replace hex color codes (6 or 8 digit) with color names."""
    try:
        with open(filepath, 'r', encoding='utf-8') as f:
            content = f.read()

        original_content = content
        replacements_made = 0

        # Match 0x followed by either 6 or 8 hex digits
        # Capture the RGB part (first 6 digits)
        pattern = re.compile(r'0x([0-9A-Fa-f]{6})(?:[0-9A-Fa-f]{2})?', re.IGNORECASE)

        def replacer(match):
            rgb = match.group(1).lower()
            if rgb in COLOR_MAP:
                return COLOR_MAP[rgb]
            else:
                # Not a known color, keep original
                return match.group(0)

        content = pattern.sub(replacer, content)

        # Count how many were actually replaced
        if content != original_content:
            replacements_made = len(pattern.findall(original_content))
            # Write the file
            with open(filepath, 'w', encoding='utf-8') as f:
                f.write(content)

        return replacements_made

    except Exception as e:
        print(f"Error processing {filepath}: {e}")
        return 0

def process_directory(base_path):
    """Recursively process all .c files in directory."""
    total_files = 0
    total_replacements = 0

    for root, dirs, files in os.walk(base_path):
        for file in files:
            if file.endswith('.c'):
                filepath = os.path.join(root, file)
                replacements = replace_colors_in_file(filepath)

                if replacements > 0:
                    total_files += 1
                    total_replacements += replacements
                    print(f"✓ {filepath}: {replacements} replacement(s)")

    return total_files, total_replacements

if __name__ == "__main__":
    # Set the base directory to search
    base_dir = "src/kernel"

    if not os.path.exists(base_dir):
        print(f"Error: Directory '{base_dir}' not found!")
        print("Make sure you're running this script from the AMQ-OS directory.")
        exit(1)

    print(f"Scanning {base_dir} for .c files...")
    print("Replacing hex color codes with color names...")
    print("-" * 50)

    files_modified, total_replacements = process_directory(base_dir)

    print("-" * 50)
    print(f"\nSummary:")
    print(f"  Files modified: {files_modified}")
    print(f"  Total replacements: {total_replacements}")

    if total_replacements == 0:
        print("\nNo color codes found to replace.")
