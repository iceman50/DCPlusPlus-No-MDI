// Copyright (C) 2026 iceman50
// Package the transparent orb artwork at every supported Windows size.
using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.IO;

public static class OrbApplicationIcon {
    public static byte[] Create(string artworkPath) {
        using(Bitmap artwork = new Bitmap(artworkPath)) {
            if(artwork.Width != artwork.Height || artwork.Width < 256)
                throw new InvalidDataException("Orb application artwork must be square and at least 256px");
            if(artwork.GetPixel(0,0).A != 0 || artwork.GetPixel(artwork.Width-1,artwork.Height-1).A != 0)
                throw new InvalidDataException("Orb application artwork must have a transparent background");
            return IconPackRenderer.CreateIcon(size => Resize(artwork,size));
        }
    }

    private static Bitmap Resize(Bitmap artwork, int size) {
        Bitmap output = new Bitmap(size,size,PixelFormat.Format32bppArgb);
        using(Graphics g = Graphics.FromImage(output))
        using(ImageAttributes attributes = new ImageAttributes()) {
            g.Clear(Color.Transparent);
            g.CompositingMode = CompositingMode.SourceCopy;
            g.CompositingQuality = CompositingQuality.HighQuality;
            g.InterpolationMode = InterpolationMode.HighQualityBicubic;
            g.PixelOffsetMode = PixelOffsetMode.HighQuality;
            attributes.SetWrapMode(WrapMode.TileFlipXY);
            // Integer padding keeps the smallest frame's filtering inside its border.
            int inset = size == 16 ? 1 : 0;
            g.DrawImage(artwork,new Rectangle(inset,inset,size-inset*2,size-inset*2),0,0,artwork.Width,artwork.Height,GraphicsUnit.Pixel,attributes);
        }
        return output;
    }
}
