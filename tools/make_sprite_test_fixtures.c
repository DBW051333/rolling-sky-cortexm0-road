/* Generate production skin data for tools/tb_sprite_writer.v.
 * Run from the repository root after creating tmp/game_accel. */
#include <stdio.h>
#include "../firmware/player.h"

int main(void) {
    unsigned skin,y,i,length,count,x;
    FILE *indices=fopen("tmp/game_accel/skins.hex","w");
    FILE *palettes=fopen("tmp/game_accel/palettes.hex","w");
    if(!indices || !palettes) {
        if(indices)fclose(indices);
        if(palettes)fclose(palettes);
        return 1;
    }
    for(skin=0;skin<PLAYER_COUNT;skin++) {
        player_select(skin);
        for(i=0;i<8;i++)fprintf(palettes,"%04x\n",player_colors()[i]);
        for(y=0;y<BALL_HEIGHT;y++) {
            const uint8_t *runs=player_row(y,&count);
            x=0;
            for(i=0;i<count;i++) {
                length=runs[i]>>3;
                while(length--) {
                    fprintf(indices,"%x\n",runs[i]&7u);
                    x++;
                }
            }
            while(x<64) { fprintf(indices,"0\n");x++; }
        }
    }
    if(fclose(indices)) { fclose(palettes);return 1; }
    return fclose(palettes)!=0;
}
