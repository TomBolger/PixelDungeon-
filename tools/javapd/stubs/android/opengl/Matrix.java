package android.opengl;
/** Real 4x4 column-major multiply (cheap, so implemented for correctness). */
public class Matrix {
	public static void multiplyMM(float[] r,int ro,float[] l,int lo,float[] rt,int rto){
		float[] t=new float[16];
		for(int i=0;i<4;i++) for(int j=0;j<4;j++){ float s=0; for(int k=0;k<4;k++) s+=l[lo+k*4+j]*rt[rto+i*4+k]; t[i*4+j]=s; }
		System.arraycopy(t,0,r,ro,16);
	}
	public static void scaleM(float[] m,int o,float x,float y,float z){ for(int i=0;i<4;i++){ m[o+i]*=x; m[o+4+i]*=y; m[o+8+i]*=z; } }
}
