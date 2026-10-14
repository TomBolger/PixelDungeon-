package android.util;
import java.util.Arrays;
/** Functional reimplementation (sorted int keys, like Android's). */
public class SparseArray<E> implements Cloneable {
	private int[] keys = new int[10]; private Object[] vals = new Object[10]; private int size = 0;
	public SparseArray(){} public SparseArray(int cap){ keys=new int[Math.max(cap,1)]; vals=new Object[Math.max(cap,1)]; }
	private int find(int key){ return Arrays.binarySearch(keys,0,size,key); }
	@SuppressWarnings("unchecked") public E get(int key){ int i=find(key); return i<0?null:(E)vals[i]; }
	@SuppressWarnings("unchecked") public E get(int key,E def){ int i=find(key); return i<0?def:(E)vals[i]; }
	public void put(int key,E v){ int i=find(key); if(i>=0){vals[i]=v;return;} i=~i;
		if(size==keys.length){ keys=Arrays.copyOf(keys,size*2+1); vals=Arrays.copyOf(vals,size*2+1); }
		System.arraycopy(keys,i,keys,i+1,size-i); System.arraycopy(vals,i,vals,i+1,size-i); keys[i]=key; vals[i]=v; size++; }
	public void append(int key,E v){ put(key,v); }
	public void delete(int key){ int i=find(key); if(i>=0) removeAt(i); }
	public void remove(int key){ delete(key); }
	public void removeAt(int i){ System.arraycopy(keys,i+1,keys,i,size-i-1); System.arraycopy(vals,i+1,vals,i,size-i-1); size--; vals[size]=null; }
	public int size(){ return size; }
	public int keyAt(int i){ return keys[i]; }
	@SuppressWarnings("unchecked") public E valueAt(int i){ return (E)vals[i]; }
	public void setValueAt(int i,E v){ vals[i]=v; }
	public int indexOfKey(int key){ int i=find(key); return i<0?-1:i; }
	public int indexOfValue(E v){ for(int i=0;i<size;i++) if(vals[i]==v) return i; return -1; }
	public void clear(){ Arrays.fill(vals,0,size,null); size=0; }
	@SuppressWarnings("unchecked") public SparseArray<E> clone(){ try{ SparseArray<E> c=(SparseArray<E>)super.clone(); c.keys=keys.clone(); c.vals=vals.clone(); return c; }catch(CloneNotSupportedException e){ throw new RuntimeException(e);} }
}
