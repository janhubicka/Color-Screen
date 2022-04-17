package cz.cuni.mff.java.digitalColoring.interfaceWithC;

public class ImageData {
    /**
     * Dimensions of image data
     */
    int width;
    int height;
    /**
     * Maximal value returned by getPixel (interpretted as white)
     */
    int maxval;

    /**
     * Return grayscale value of a pixel at coordinates X and Y
     */
    final public int getPixel (int x, int y)
    {
       return getPixelHelper (nativeData, x, y);
    }

    /**
     * Load FILENAME
     */
    final public native void load(String filename);

    /* Pointer to C++ datastructure.  */
    private long nativeData;
    final private native int getPixelHelper (long nativeData, int x, int y);
}
