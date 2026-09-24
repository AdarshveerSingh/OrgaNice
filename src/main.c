
#include <stdio.h>

void displayMenu(void) {
    printf("\n=================================\n");
    printf("       SMART FILE MANAGER        \n");
    printf("=================================\n");

    printf("1. Scan Directory\n");
    printf("2. Organize Files\n");
    printf("3. Find Duplicates\n");
    printf("4. Cleanup Analyzer\n");
    printf("5. Search Files\n");
    printf("6. Operation History\n");
    printf("0. Exit\n");

    printf("=================================\n");
    printf("Enter your choice: ");
}

int main(void) {
    int choice;

    do {
        displayMenu();

        if (scanf("%d", &choice) != 1) {
            printf("Invalid input. Please enter a number.\n");

            int ch;
            while ((ch = getchar()) != '\n' && ch != EOF);

            continue;
        }

        switch (choice) {
            case 1:
                printf("\nDirectory scanner coming soon.\n");
                break;

            case 2:
                printf("\nFile organizer coming soon.\n");
                break;

            case 3:
                printf("\nDuplicate detector coming soon.\n");
                break;

            case 4:
                printf("\nCleanup analyzer coming soon.\n");
                break;

            case 5:
                printf("\nFile search coming soon.\n");
                break;

            case 6:
                printf("\nOperation history coming soon.\n");
                break;

            case 0:
                printf("\nExiting Smart File Manager.\n");
                break;

            default:
                printf("\nInvalid choice. Try again.\n");
        }

    } while (choice != 0);

    return 0;
}
