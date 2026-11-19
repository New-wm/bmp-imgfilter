/*
 * imgfilter.c —— 纯 C 实现的 BMP 图像滤镜工具
 *
 * 支持：读取 24 位未压缩 BMP，执行滤镜后另存为 BMP
 * 滤镜：gray（灰度）invert（反色）blur（均值模糊）edge（Sobel 边缘）
 *
 * 编译：gcc imgfilter.c -o imgfilter.exe
 * 用法：imgfilter.exe 输入.bmp 输出.bmp gray
 *
 * 说明：不依赖任何第三方库，手动解析 BMP 头；变量均在块首声明，兼容 C89
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int width;
    int height;
    int rowBytes;      /* 每行字节数（4 字节对齐后的实际宽度） */
    int dataSize;
    unsigned char header[54];   /* 原始文件头 + 信息头，回写时原样复制 */
    unsigned char *pix;         /* 像素数据，BGR 顺序，自底向上 */
} Image;

/* ---------------- BMP 读取 ---------------- */
int readBmp(const char *path, Image *img)
{
    FILE *f;
    int bits, comp, i;
    long fileSize;

    f = fopen(path, "rb");
    if (!f) { printf("Cannot open file: %s\n", path); return 0; }

    if (fread(img->header, 1, 54, f) != 54) { fclose(f); return 0; }

    if (img->header[0] != 'B' || img->header[1] != 'M') {
        printf("Not a BMP file\n"); fclose(f); return 0;
    }

    fileSize = *(int *)(img->header + 2);
    bits = *(short *)(img->header + 28);
    comp = *(int *)(img->header + 30);

    if (bits != 24 || comp != 0) {
        printf("Only 24-bit uncompressed BMP is supported (depth %d, compression %d)\n", bits, comp);
        fclose(f); return 0;
    }

    img->width = *(int *)(img->header + 18);
    img->height = *(int *)(img->header + 22);
    if (img->height < 0) img->height = -img->height;   /* 兼容自顶向下存储 */

    img->rowBytes = ((img->width * 3 + 3) / 4) * 4;
    img->dataSize = img->rowBytes * img->height;

    img->pix = (unsigned char *)malloc(img->dataSize);
    if (!img->pix) { fclose(f); return 0; }

    fseek(f, *(int *)(img->header + 10), SEEK_SET);    /* 跳到像素数据起点 */
    if (fread(img->pix, 1, img->dataSize, f) != (size_t)img->dataSize) {
        printf("Pixel data read incomplete\n");
        free(img->pix); img->pix = NULL; fclose(f); return 0;
    }
    fclose(f);

    printf("Loaded: %d x %d, rowBytes %d\n", img->width, img->height, img->rowBytes);
    return 1;
}

/* ---------------- BMP 写出 ---------------- */
int writeBmp(const char *path, Image *img)
{
    FILE *f;

    f = fopen(path, "wb");
    if (!f) { printf("Cannot write file: %s\n", path); return 0; }
    fwrite(img->header, 1, 54, f);
    fwrite(img->pix, 1, img->dataSize, f);
    fclose(f);
    printf("Saved: %s\n", path);
    return 1;
}

/* ---------------- 滤镜：灰度 ---------------- */
void filterGray(Image *img)
{
    int y, x, i;
    unsigned char *p;

    for (y = 0; y < img->height; y++) {
        p = img->pix + y * img->rowBytes;
        for (x = 0; x < img->width; x++) {
            i = x * 3;
            int b = p[i], g = p[i + 1], r = p[i + 2];
            int v = (int)(0.299 * r + 0.587 * g + 0.114 * b);
            p[i] = p[i + 1] = p[i + 2] = (unsigned char)v;
        }
    }
}

/* ---------------- 滤镜：反色 ---------------- */
void filterInvert(Image *img)
{
    int y, x, i;
    unsigned char *p;

    for (y = 0; y < img->height; y++) {
        p = img->pix + y * img->rowBytes;
        for (x = 0; x < img->width; x++) {
            i = x * 3;
            p[i] = 255 - p[i];
            p[i + 1] = 255 - p[i + 1];
            p[i + 2] = 255 - p[i + 2];
        }
    }
}

/* ---------------- 滤镜：均值模糊（3x3）---------------- */
void filterBlur(Image *img)
{
    int y, x, dy, dx, i, j;
    unsigned char *src, *dst, *p, *q;
    int sr, sg, sb, cnt;

    src = (unsigned char *)malloc(img->dataSize);      /* 用副本读，避免边算边改 */
    if (!src) return;
    memcpy(src, img->pix, img->dataSize);

    for (y = 0; y < img->height; y++) {
        p = img->pix + y * img->rowBytes;
        for (x = 0; x < img->width; x++) {
            sr = sg = sb = 0;
            cnt = 0;
            for (dy = -1; dy <= 1; dy++) {
                for (dx = -1; dx <= 1; dx++) {
                    int ny = y + dy, nx = x + dx;
                    if (ny < 0 || ny >= img->height || nx < 0 || nx >= img->width) continue;
                    q = src + ny * img->rowBytes + nx * 3;
                    sb += q[0]; sg += q[1]; sr += q[2];
                    cnt++;
                }
            }
            i = x * 3;
            p[i] = (unsigned char)(sb / cnt);
            p[i + 1] = (unsigned char)(sg / cnt);
            p[i + 2] = (unsigned char)(sr / cnt);
        }
    }
    free(src);
}

/* ---------------- 滤镜：Sobel 边缘检测 ---------------- */
void filterEdge(Image *img)
{
    int y, x, dy, dx, i, gx, gy, mag;
    int *gray;
    unsigned char *q;

    gray = (int *)malloc(sizeof(int) * img->width * img->height);
    if (!gray) return;

    /* 先转灰度，边缘检测在亮度上做 */
    for (y = 0; y < img->height; y++) {
        q = img->pix + y * img->rowBytes;
        for (x = 0; x < img->width; x++) {
            i = x * 3;
            gray[y * img->width + x] = (int)(0.299 * q[i + 2] + 0.587 * q[i + 1] + 0.114 * q[i]);
        }
    }

    for (y = 0; y < img->height; y++) {
        q = img->pix + y * img->rowBytes;
        for (x = 0; x < img->width; x++) {
            gx = gy = 0;
            if (x > 0 && x < img->width - 1 && y > 0 && y < img->height - 1) {
                int a = gray[(y - 1) * img->width + (x - 1)];
                int b = gray[(y - 1) * img->width + x];
                int c = gray[(y - 1) * img->width + (x + 1)];
                int d = gray[y * img->width + (x - 1)];
                int e = gray[y * img->width + (x + 1)];
                int f2 = gray[(y + 1) * img->width + (x - 1)];
                int g2 = gray[(y + 1) * img->width + x];
                int h2 = gray[(y + 1) * img->width + (x + 1)];
                gx = (c + 2 * e + h2) - (a + 2 * d + f2);
                gy = (f2 + 2 * g2 + h2) - (a + 2 * b + c);
                mag = abs(gx) + abs(gy);
                if (mag > 255) mag = 255;
            } else {
                mag = 0;      /* 边界像素留黑，避免越界读 */
            }
            i = x * 3;
            q[i] = q[i + 1] = q[i + 2] = (unsigned char)mag;
        }
    }
    free(gray);
}

/* ---------------- 主函数 ---------------- */
int main(int argc, char **argv)
{
    Image img;
    const char *mode;

    if (argc < 4) {
        printf("Usage: imgfilter.exe <input.bmp> <output.bmp> [gray|invert|blur|edge]\n");
        return 1;
    }

    mode = argv[3];
    if (strcmp(mode, "gray") && strcmp(mode, "invert") &&
        strcmp(mode, "blur") && strcmp(mode, "edge")) {
        printf("Unknown filter: %s (gray / invert / blur / edge)\n", mode);
        return 1;
    }

    memset(&img, 0, sizeof(img));
    if (!readBmp(argv[1], &img)) return 1;

    if (!strcmp(mode, "gray"))   filterGray(&img);
    if (!strcmp(mode, "invert")) filterInvert(&img);
    if (!strcmp(mode, "blur"))   filterBlur(&img);
    if (!strcmp(mode, "edge"))   filterEdge(&img);

    writeBmp(argv[2], &img);
    free(img.pix);
    return 0;
}
