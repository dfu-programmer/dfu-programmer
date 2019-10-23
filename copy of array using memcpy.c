/* cpying array using memcpy function*/
#include<stdio.h>
int main()
{
	int i;
	int a[]={3,4,5,6,7,8,9};
	int b[7];
	for(i=0;i<=5;i++)
	{
		memcpy(b,a,sizeof(a));
		printf("%d\n",*(b+i));
	}
}
