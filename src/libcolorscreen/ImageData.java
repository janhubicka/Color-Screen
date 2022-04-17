package cz.cuni.mff.java.digitalColoring.interfaceWithC;

public class ImageData {
    static {
        System.loadLibrary("native");
    }

    /**
     * Dimensions of image data
     */
    int width;
    int height;
    /**
     * Maximal value of the image data.
     */
    int maxval;
    public static void main(String[] args) {
        ImageData obj = new ImageData();
	long start = System.currentTimeMillis();
	obj.load("big.tif");
	System.out.format("width=%d height=%d maxval=%d time=%d\n", obj.width, obj.height, obj.maxval, System.currentTimeMillis () - start);
	for (int i = 0; i < 50; i++)
	  {
	    for (int j = 0; j < 160; j++)
	      {
		int pixel = obj.getPixel (j * obj.width / 160, i * obj.height / 50);
		if (pixel < obj.maxval / 4)
		  System.out.format(" ");
		else if (pixel < obj.maxval / 2)
		  System.out.format(".");
		else if (pixel < 3 * obj.maxval / 4)
		  System.out.format("x");
		else 
		  System.out.format("X");
	      }
	    System.out.format("\n");
	  }
	start = System.currentTimeMillis();
	double sum = 0;
        for (int j = 0; j < obj.height; j++)
	  for (int i = 0; i < obj.width; i++)
	    sum += obj.getPixel (i, j);
	System.out.format("avg:%f time:%d\n", sum / obj.width / obj.height, System.currentTimeMillis () - start);

	int[][] img = new int[obj.height][obj.width];
        for (int j = 0; j < obj.height; j++)
	  for (int i = 0; i < obj.width; i++)
	    img[j][i] += obj.getPixel (i, j);
	System.out.format("copying done\n");

	start = System.currentTimeMillis();
	sum = 0;
        for (int j = 0; j < obj.height; j++)
	  for (int i = 0; i < obj.width; i++)
	    sum += img[j][i];
	System.out.format("native avg:%f time:%d\n", sum / obj.width / obj.height, System.currentTimeMillis () - start);
    }
    final public int getPixel (int x, int y)
    {
       return getPixelHelper (nativeData, x, y);
    }

    /* Pointer to C++ datastructure.  */
    private long nativeData;
    private native void load(String filename);
    private native int getPixelHelper (long nativeData, int x, int y);
}
