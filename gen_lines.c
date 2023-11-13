#include <stdio.h>

#include "structs.h"

int main()
{
	for(int i=1;i<=24;i++)
	{
		fprintf(stderr, "doing %d\n", i);
		// horizontal is easy
		int horizontal[3];
		{
			int first = ((i-1)/3)*3+1;
			for(int j=0;j<3;j++)
				horizontal[j] = first+j;
		}
		// now vertical
		uint8_t *nei = board_moves[i];
		int vertical[3];
		{
			// are we mid or point?
			int prev = 0, next = 0;
			int mid = i;
			fprintf(stderr, "checking neighbours\n");
			for(uint8_t *ptr = nei; *ptr; ptr++)
			{
				fprintf(stderr, "neighbor is %d\n", *ptr);
				int diff = i-*ptr;
				if(diff == -1 || diff == 1)
					// not this
					continue;
				// else
				if(diff > 0)
					prev = *ptr;
				else
					next = *ptr;
			}
			fprintf(stderr, "first pass: %d %d %d\n", prev, i, next);
			if(!prev&&!next)
			{
				// now that's weird
				printf("Got no prev or next for %d\n", i);
				return 1;
			}
			if(!prev||!next)
			{
				// which is missing?
				if(!prev)
					mid = next;
				else
					mid = prev;
				// fetch from mid
				for(uint8_t *ptr = board_moves[mid];*ptr;ptr++)
				{
					int diff = mid-*ptr;
					if(diff == -1 || diff == 1)
						continue;
					if(diff > 0)
						prev = *ptr;
					else
						next = *ptr;
				}
			}
			vertical[0] = prev;
			vertical[1] = mid;
			vertical[2] = next;
		}
		// print it
		printf("\t[%2d] = {{", i);
		int *h = horizontal;
		int *v = vertical;
		printf("{{%2d,%2d,%2d}},", h[0],h[1],h[2]);
		printf("{{%2d,%2d,%2d}}}},\n", v[0],v[1],v[2]);
	}
	return 0;
}
