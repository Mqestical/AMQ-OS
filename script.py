#!/usr/bin/env python3
import os
import sys
import re

def remove_comments(file_path):
    """Remove // comments from a file, including inline comments."""
    try:
        with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
            lines = f.readlines()

        filtered_lines = []
        removed_count = 0

        for line in lines:
            original = line

            # Remove inline // comments (preserve code before //)
            # This handles: "code here  // comment"
            if '//' in line:
                # Find the position of //
                comment_pos = line.find('//')

                # Check if it's actually a comment (not in a string literal)
                # Simple approach: check if // is outside quotes
                in_string = False
                quote_char = None
                escaped = False

                for i, char in enumerate(line[:comment_pos]):
                    if escaped:
                        escaped = False
                        continue
                    if char == '\\':
                        escaped = True
                        continue
                    if char in ['"', "'"]:
                        if not in_string:
                            in_string = True
                            quote_char = char
                        elif char == quote_char:
                            in_string = False
                            quote_char = None

                # If // is not inside a string, remove the comment
                if not in_string:
                    line_before_comment = line[:comment_pos].rstrip()

                    # If line only had a comment (nothing before //), skip it entirely
                    if not line_before_comment:
                        removed_count += 1
                        continue
                    else:
                        # Keep the code, add back newline
                        line = line_before_comment + '\n'

            filtered_lines.append(line)
            if original != line:
                removed_count += 1

        # Only write if changes were made
        if removed_count > 0:
            with open(file_path, 'w', encoding='utf-8') as f:
                f.writelines(filtered_lines)
            print(f"✓ {file_path}: Modified {removed_count} line(s)")
            return removed_count
        return 0
    except Exception as e:
        print(f"✗ Error processing {file_path}: {e}", file=sys.stderr)
        return 0

def process_directory(root_dir, extensions=None):
    """Recursively process all files in directory."""
    if extensions is None:
        extensions = {'.c', '.h', '.cpp', '.hpp', '.cc', '.cxx'}

    total_removed = 0
    files_processed = 0

    for dirpath, dirnames, filenames in os.walk(root_dir):
        for filename in filenames:
            # Check if file has a relevant extension
            if any(filename.endswith(ext) for ext in extensions):
                file_path = os.path.join(dirpath, filename)
                removed = remove_comments(file_path)
                if removed > 0:
                    files_processed += 1
                total_removed += removed

    print(f"\n{'='*50}")
    print(f"Summary: Processed {files_processed} file(s)")
    print(f"Total lines modified: {total_removed}")
    print(f"{'='*50}")

if __name__ == "__main__":
    # Use current directory if no argument provided
    target_dir = sys.argv[1] if len(sys.argv) > 1 else '.'

    if not os.path.isdir(target_dir):
        print(f"Error: '{target_dir}' is not a valid directory", file=sys.stderr)
        sys.exit(1)

    print(f"Processing files in: {os.path.abspath(target_dir)}")
    print(f"{'='*50}")

    # Ask for confirmation
    response = input("This will modify files. Continue? (y/n): ")
    if response.lower() != 'y':
        print("Aborted.")
        sys.exit(0)

    process_directory(target_dir)
