import sys
data = open(sys.argv[1], "rb").read()
out = open(sys.argv[2], "w")
out.write('#ifdef __cplusplus\nextern "C" {\n#endif\n')
out.write("unsigned char bootloader_embed[] = {\n")
for i, b in enumerate(data):
    out.write("0x%02x,%s" % (b, "\n" if i % 12 == 11 else ""))
out.write("\n};\n")
out.write("unsigned int bootloader_embed_len = %d;\n" % len(data))
out.write('#ifdef __cplusplus\n}\n#endif\n')
out.close()
